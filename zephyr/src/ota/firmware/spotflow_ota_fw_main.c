#include "ota/firmware/spotflow_ota_fw_main.h"

#include <spotflow/downloader.h>

#include "ota/downloader/spotflow_ota_downloader.h"
#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/platform/spotflow_ota_identity.h"
#include "ota/core/spotflow_ota_log.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/platform/spotflow_ota_platform.h"
#include "ota/core/spotflow_ota_state.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

struct main_firmware_flash_ctx {
	int write_err;
};

static SPOTFLOW_DEFINE_DOWNLOADER(main_firmware_downloader);

static K_SEM_DEFINE(main_firmware_resume_sem, 0, 1);

static void notify_main_firmware_phase(enum spotflow_ota_phase phase);
static void notify_main_firmware_state(const struct spotflow_ota_main_firmware_state* state);
static enum spotflow_ota_result fail_main_firmware(void);
static void fill_main_firmware_state_output(struct spotflow_ota_main_firmware_state* out_state);
static void main_firmware_wake_paused_worker(void);
static void main_firmware_drain_resume_sem(void);
static void wait_while_paused(bool honor_interruptions);
static int main_firmware_control_checkpoint(void);
static enum spotflow_ota_result interrupted_main_firmware_result(void);
static int begin_main_firmware_upgrade_commit(void);
static int begin_main_firmware_reboot(void);
static void download_started_cb(struct spotflow_downloader* downloader, void* callback_ctx);
static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx);
static int complete_main_firmware_success(const struct spotflow_ota_probation* probation,
					  spotflow_ota_state_effects* effects);
static int complete_main_firmware_rollback(const struct spotflow_ota_probation* probation,
					   spotflow_ota_state_effects* effects);
static bool main_artifact_is_pending(const struct spotflow_ota_probation* probation);

void spotflow_ota_fw_main_reset(void)
{
	k_mutex_lock(&main_firmware_downloader.mutex, K_FOREVER);
	main_firmware_downloader.state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	main_firmware_downloader.cancel_requested = false;
	k_mutex_unlock(&main_firmware_downloader.mutex);
	while (k_sem_take(&main_firmware_downloader.resume_sem, K_NO_WAIT) == 0) {
	}
	main_firmware_drain_resume_sem();
}

enum spotflow_ota_result
spotflow_ota_fw_main_process_artifact(uint64_t attempt_id, size_t artifact_index,
				      const struct spotflow_ota_artifact* artifact)
{
	if (artifact == NULL || artifact->url[0] == '\0' || artifact->secret[0] == '\0') {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	LOG_INF("OTA attempt %llu: started main firmware artifact '%s' %s",
		(unsigned long long)attempt_id, artifact->slug, artifact->version);

	if (spotflow_ota_state_store_main_firmware_artifact(attempt_id, artifact_index, artifact) <
	    0) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	if (main_firmware_control_checkpoint() < 0) {
		return interrupted_main_firmware_result();
	}

	if (spotflow_is_update_canceled()) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD);

	if (main_firmware_control_checkpoint() < 0) {
		return interrupted_main_firmware_result();
	}

	struct spotflow_download_request request = {
		.url = artifact->url,
		.secret = artifact->secret,
	};
	struct main_firmware_flash_ctx flash_ctx = {
		.write_err = 0,
	};
	int rc = spotflow_ota_platform_begin_image_write();

	if (rc < 0) {
		LOG_ERR("Failed to initialize main firmware image writer: %d", rc);
		return fail_main_firmware();
	}

	rc = spotflow_ota_download_artifact(&main_firmware_downloader, &request, download_block_cb,
					    &flash_ctx, download_started_cb, NULL);
	if (flash_ctx.write_err != 0) {
		LOG_ERR("Main firmware flash write failed: %d", flash_ctx.write_err);
		return fail_main_firmware();
	}
	if (rc == -ECANCELED) {
		return interrupted_main_firmware_result();
	}
	if (rc < 0) {
		LOG_ERR("Main firmware download failed: %d", rc);
		return fail_main_firmware();
	}

	if (spotflow_is_update_canceled()) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	if (main_firmware_control_checkpoint() < 0) {
		return interrupted_main_firmware_result();
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_UPGRADE);

	if (main_firmware_control_checkpoint() < 0) {
		return interrupted_main_firmware_result();
	}

	uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH];

	rc = spotflow_ota_identity_get_downloaded_build_id(expected_build_id);
	if (rc < 0) {
		LOG_ERR("Failed to read downloaded main firmware build ID: %d", rc);
		return fail_main_firmware();
	}

	struct spotflow_ota_probation probation = {
		.attempt_id = attempt_id,
		.artifact_index = (uint32_t)artifact_index,
	};

	strncpy(probation.slug, artifact->slug, sizeof(probation.slug) - 1);
	strncpy(probation.version, artifact->version, sizeof(probation.version) - 1);
	memcpy(probation.expected_build_id, expected_build_id, sizeof(probation.expected_build_id));

	rc = begin_main_firmware_upgrade_commit();
	if (rc < 0) {
		return rc == -ECANCELED ? interrupted_main_firmware_result() : fail_main_firmware();
	}

	rc = spotflow_ota_persistence_save_probation(&probation);
	if (rc < 0) {
		LOG_ERR("Failed to persist main firmware probation record: %d", rc);
		spotflow_ota_state_cancel_main_firmware_upgrade_commit();
		return fail_main_firmware();
	}

	rc = spotflow_ota_platform_request_test_upgrade();
	if (rc < 0) {
		LOG_ERR("Failed to request main firmware test upgrade: %d", rc);
		spotflow_ota_state_cancel_main_firmware_upgrade_commit();
		return fail_main_firmware();
	}

	rc = spotflow_ota_state_finish_main_firmware_prereboot();
	if (rc < 0) {
		LOG_ERR("Failed to finalize main firmware before reboot: %d", rc);
		return fail_main_firmware();
	}

	struct spotflow_ota_main_firmware_state pending_reboot_state;

	rc = spotflow_ota_state_set_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_REBOOT,
							&pending_reboot_state);
	if (rc < 0) {
		LOG_ERR("Failed to enter main firmware pending-reboot phase: %d", rc);
		return fail_main_firmware();
	}

	LOG_INF("Main firmware phase -> %s",
		spotflow_ota_log_phase_name(SPOTFLOW_OTA_PHASE_PENDING_REBOOT));
	notify_main_firmware_state(&pending_reboot_state);

	rc = begin_main_firmware_reboot();
	if (rc < 0) {
		LOG_ERR("Failed to begin main firmware reboot: %d", rc);
		return fail_main_firmware();
	}

	spotflow_ota_platform_reboot();

	/*
	 * spotflow_ota_platform_reboot() does not return on hardware. The return below keeps
	 * unit tests with a returning platform fake well-defined.
	 */
	return SPOTFLOW_OTA_RESULT_PENDING;
}

int spotflow_ota_fw_main_reconcile_startup(const struct spotflow_ota_probation* probation,
					   bool has_probation, spotflow_ota_state_effects* effects)
{
	if (effects == NULL) {
		return -EINVAL;
	}

	*effects = 0;

	if (!has_probation || probation == NULL) {
		LOG_DBG("No main firmware probation record to reconcile");
		return 0;
	}

	LOG_DBG("Reconciling main firmware probation for OTA attempt %llu ('%s' %s)",
		(unsigned long long)probation->attempt_id, probation->slug, probation->version);

	if (!main_artifact_is_pending(probation)) {
		int rc = spotflow_ota_persistence_clear_probation();

		if (rc < 0) {
			LOG_ERR("Failed to clear stale main firmware probation record: %d", rc);
			return rc;
		}

		return 0;
	}

	enum spotflow_ota_identity_cmp identity =
		spotflow_ota_identity_compare_probation(probation->expected_build_id);

	if (identity == SPOTFLOW_OTA_IDENTITY_UNAVAILABLE) {
		LOG_WRN("Main firmware probation record present but running identity unavailable: "
			"reporting failure");
		return complete_main_firmware_rollback(probation, effects);
	}

	if (identity == SPOTFLOW_OTA_IDENTITY_MISMATCH) {
		LOG_INF("Main firmware rollback detected for OTA attempt %llu ('%s' %s): "
			"reporting failure",
			(unsigned long long)probation->attempt_id, probation->slug,
			probation->version);
		return complete_main_firmware_rollback(probation, effects);
	}

	if (!spotflow_ota_platform_is_image_confirmed()) {
		struct spotflow_ota_main_firmware_state state;
		int rc = spotflow_ota_state_enter_main_firmware_unconfirmed(&state);

		if (rc < 0) {
			return rc;
		}

		LOG_DBG("Main firmware rebooted into unconfirmed image for OTA attempt %llu",
			(unsigned long long)probation->attempt_id);
		notify_main_firmware_state(&state);
		return 0;
	}

	LOG_DBG("Main firmware already confirmed for OTA attempt %llu",
		(unsigned long long)probation->attempt_id);
	return complete_main_firmware_success(probation, effects);
}

int spotflow_ota_fw_main_confirm_image(struct spotflow_ota_main_firmware_state* out_state,
				       spotflow_ota_state_effects* effects)
{
	struct spotflow_ota_main_firmware_view view;
	struct spotflow_ota_probation probation;
	bool has_probation;
	int rc;

	if (effects == NULL) {
		return -EINVAL;
	}

	*effects = 0;

	spotflow_ota_state_get_main_firmware_view(&view);
	if (!view.has_current_attempt) {
		if (out_state != NULL) {
			*out_state = view.state;
		}

		return -EINVAL;
	}

	if (view.state.phase == SPOTFLOW_OTA_PHASE_NOT_RUNNING &&
	    view.state.result == SPOTFLOW_OTA_RESULT_SUCCEEDED &&
	    spotflow_ota_platform_is_image_confirmed()) {
		if (out_state != NULL) {
			*out_state = view.state;
		}

		return 0;
	}

	rc = spotflow_ota_persistence_load_probation(&probation, &has_probation);
	if (rc < 0) {
		return rc;
	}

	if (has_probation && probation.attempt_id == view.attempt_id &&
	    spotflow_ota_identity_compare_probation(probation.expected_build_id) ==
		    SPOTFLOW_OTA_IDENTITY_MATCH &&
	    spotflow_ota_platform_is_image_confirmed() &&
	    view.state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		rc = complete_main_firmware_success(&probation, effects);
		if (rc < 0) {
			return rc;
		}

		if (out_state != NULL) {
			spotflow_ota_state_get_main_firmware_view(&view);
			*out_state = view.state;
		}

		return 0;
	}

	if (view.state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		LOG_ERR("Main firmware confirmation rejected: has_attempt=%d phase=%d result=%d",
			view.has_current_attempt, view.state.phase, view.state.result);
		if (out_state != NULL) {
			*out_state = view.state;
		}

		return -EINVAL;
	}

	if (!has_probation || probation.attempt_id != view.attempt_id) {
		LOG_ERR("Main firmware confirmation rejected: has_probation=%d attempt_id=%llu "
			"current_attempt_id=%llu",
			has_probation, (unsigned long long)probation.attempt_id,
			(unsigned long long)view.attempt_id);
		if (out_state != NULL) {
			*out_state = view.state;
		}

		return -EINVAL;
	}

	rc = spotflow_ota_platform_confirm_image();
	if (rc < 0) {
		if (out_state != NULL) {
			*out_state = view.state;
		}

		return rc;
	}

	rc = complete_main_firmware_success(&probation, effects);
	if (rc < 0) {
		return rc;
	}

	if (out_state != NULL) {
		spotflow_ota_state_get_main_firmware_view(&view);
		*out_state = view.state;
	}

	return 0;
}

int spotflow_ota_fw_main_pause_update(struct spotflow_ota_main_firmware_state* out_state)
{
	enum spotflow_downloader_state downloader_state;
	int rc;

	rc = spotflow_ota_state_set_main_firmware_paused(true, out_state);
	if (rc < 0) {
		fill_main_firmware_state_output(out_state);
		return rc;
	}

	downloader_state = spotflow_get_downloader_state(&main_firmware_downloader);
	if (downloader_state == SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING) {
		rc = spotflow_pause_download(&main_firmware_downloader);
		if (rc < 0 && rc != -EINVAL) {
			return rc;
		}
	}

	return 0;
}

int spotflow_ota_fw_main_resume_update(struct spotflow_ota_main_firmware_state* out_state)
{
	struct spotflow_ota_main_firmware_view view;
	enum spotflow_downloader_state downloader_state;
	int rc;

	spotflow_ota_state_get_main_firmware_view(&view);
	if (!view.has_current_attempt || !view.state.is_paused) {
		fill_main_firmware_state_output(out_state);
		return -EINVAL;
	}

	rc = spotflow_ota_state_set_main_firmware_paused(false, out_state);
	if (rc < 0) {
		return rc;
	}

	downloader_state = spotflow_get_downloader_state(&main_firmware_downloader);
	if (downloader_state == SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
		rc = spotflow_resume_download(&main_firmware_downloader);
		if (rc < 0) {
			(void)spotflow_ota_state_set_main_firmware_paused(true, NULL);
			fill_main_firmware_state_output(out_state);
			return rc;
		}
	} else {
		main_firmware_wake_paused_worker();
	}

	return 0;
}

int spotflow_ota_fw_main_fail_update(struct spotflow_ota_main_firmware_state* out_state)
{
	int rc;

	rc = spotflow_ota_state_request_main_firmware_abort(out_state);
	if (rc < 0) {
		fill_main_firmware_state_output(out_state);
		return rc;
	}

	spotflow_ota_fw_main_cancel_active_download();
	return 0;
}

void spotflow_ota_fw_main_wake_if_paused(void)
{
	struct spotflow_ota_main_firmware_view view;

	spotflow_ota_state_get_main_firmware_view(&view);
	if (view.has_current_attempt && view.state.is_paused) {
		main_firmware_wake_paused_worker();
	}
}

void spotflow_ota_fw_main_cancel_active_download(void)
{
	enum spotflow_downloader_state downloader_state =
		spotflow_get_downloader_state(&main_firmware_downloader);

	if (downloader_state == SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING ||
	    downloader_state == SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
		(void)spotflow_cancel_download(&main_firmware_downloader);
	}

	main_firmware_wake_paused_worker();
}

static void notify_main_firmware_phase(enum spotflow_ota_phase phase)
{
	struct spotflow_ota_main_firmware_state state;

	if (spotflow_ota_state_set_main_firmware_phase(phase, &state) < 0) {
		return;
	}

	LOG_INF("Main firmware phase -> %s", spotflow_ota_log_phase_name(phase));

	notify_main_firmware_state(&state);
}

static void notify_main_firmware_state(const struct spotflow_ota_main_firmware_state* state)
{
	if (state == NULL) {
		return;
	}

	spotflow_on_main_firmware_update_progressed(state);
}

static enum spotflow_ota_result fail_main_firmware(void)
{
	struct spotflow_ota_main_firmware_state state;

	if (spotflow_ota_state_set_main_firmware_result(SPOTFLOW_OTA_RESULT_FAILED, &state) == 0) {
		notify_main_firmware_state(&state);
	}

	return SPOTFLOW_OTA_RESULT_FAILED;
}

static void fill_main_firmware_state_output(struct spotflow_ota_main_firmware_state* out_state)
{
	struct spotflow_ota_main_firmware_view view;

	if (out_state == NULL) {
		return;
	}

	spotflow_ota_state_get_main_firmware_view(&view);
	*out_state = view.state;
}

static void main_firmware_wake_paused_worker(void)
{
	k_sem_give(&main_firmware_resume_sem);
}

static void main_firmware_drain_resume_sem(void)
{
	while (k_sem_take(&main_firmware_resume_sem, K_NO_WAIT) == 0) {
	}
}

static void wait_while_paused(bool honor_interruptions)
{
	struct spotflow_ota_main_firmware_view view;

	for (;;) {
		if (honor_interruptions &&
		    (spotflow_ota_state_is_main_firmware_abort_requested() ||
		     spotflow_is_update_canceled())) {
			return;
		}

		spotflow_ota_state_get_main_firmware_view(&view);
		if (!view.state.is_paused) {
			return;
		}

		k_sem_take(&main_firmware_resume_sem, K_FOREVER);
	}
}

static int main_firmware_control_checkpoint(void)
{
	wait_while_paused(true);

	if (spotflow_ota_state_is_main_firmware_abort_requested() ||
	    spotflow_is_update_canceled()) {
		return -ECANCELED;
	}

	return 0;
}

static enum spotflow_ota_result interrupted_main_firmware_result(void)
{
	if (spotflow_ota_state_is_main_firmware_abort_requested()) {
		return fail_main_firmware();
	}

	return SPOTFLOW_OTA_RESULT_CANCELED;
}

static int begin_main_firmware_upgrade_commit(void)
{
	for (;;) {
		int rc = main_firmware_control_checkpoint();

		if (rc < 0) {
			return rc;
		}

		rc = spotflow_ota_state_begin_main_firmware_upgrade_commit();
		if (rc != -EAGAIN) {
			return rc;
		}
	}
}

static int begin_main_firmware_reboot(void)
{
	for (;;) {
		int rc;

		wait_while_paused(false);

		rc = spotflow_ota_state_begin_main_firmware_reboot();
		if (rc != -EAGAIN) {
			return rc;
		}
	}
}

static void download_started_cb(struct spotflow_downloader* downloader, void* callback_ctx)
{
	struct spotflow_ota_main_firmware_view view;

	ARG_UNUSED(callback_ctx);

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_DOWNLOADING);
	spotflow_ota_state_get_main_firmware_view(&view);

	if (spotflow_ota_state_is_main_firmware_abort_requested() ||
	    spotflow_is_update_canceled()) {
		(void)spotflow_cancel_download(downloader);
	} else if (view.state.is_paused) {
		(void)spotflow_pause_download(downloader);
	}
}

static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx)
{
	struct main_firmware_flash_ctx* ctx = callback_ctx;

	if (ctx->write_err != 0) {
		return;
	}

	if (block->data_len == 0 && !block->is_last) {
		return;
	}

	ctx->write_err = spotflow_ota_platform_write_image_block(block->data, block->data_len,
								 block->is_last);
	if (ctx->write_err != 0) {
		(void)spotflow_cancel_download(downloader);
	}
}

static int complete_main_firmware_success(const struct spotflow_ota_probation* probation,
					  spotflow_ota_state_effects* effects)
{
	struct spotflow_ota_main_firmware_state state;
	int rc;

	LOG_INF("Main firmware update succeeded for OTA attempt %llu ('%s' %s)",
		(unsigned long long)probation->attempt_id, probation->slug, probation->version);

	rc = spotflow_ota_state_queue_main_firmware_result(
		probation->attempt_id, probation->artifact_index, SPOTFLOW_OTA_RESULT_SUCCEEDED,
		&state, effects);
	if (rc < 0) {
		return rc;
	}

	notify_main_firmware_state(&state);
	return 0;
}

static int complete_main_firmware_rollback(const struct spotflow_ota_probation* probation,
					   spotflow_ota_state_effects* effects)
{
	struct spotflow_ota_main_firmware_state state;
	int rc;

	rc = spotflow_ota_state_queue_main_firmware_result(
		probation->attempt_id, probation->artifact_index, SPOTFLOW_OTA_RESULT_FAILED,
		&state, effects);
	if (rc < 0) {
		return rc;
	}

	notify_main_firmware_state(&state);
	return 0;
}

static bool main_artifact_is_pending(const struct spotflow_ota_probation* probation)
{
	return spotflow_ota_state_is_main_artifact_pending(probation->attempt_id,
							   probation->artifact_index);
}
