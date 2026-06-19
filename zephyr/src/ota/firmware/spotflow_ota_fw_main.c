#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <spotflow/downloader.h>

#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/firmware/spotflow_ota_fw_main.h"
#include "ota/platform/spotflow_ota_identity.h"
#include "ota/core/spotflow_ota_log.h"
#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/platform/spotflow_ota_platform.h"
#include "ota/core/spotflow_ota_state.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static SPOTFLOW_DEFINE_DOWNLOADER(main_firmware_downloader);

static K_SEM_DEFINE(main_firmware_resume_sem, 0, 1);

static bool user_fail_requested;

struct main_firmware_flash_ctx {
	int write_err;
};

static void notify_main_firmware_phase(enum spotflow_ota_phase phase);
static void notify_main_firmware_state(const struct spotflow_ota_main_firmware_state* state);
static enum spotflow_ota_result fail_main_firmware(void);
static bool main_firmware_phase_allows_pause(enum spotflow_ota_phase phase);
static bool main_firmware_phase_allows_fail(enum spotflow_ota_phase phase);
static void fill_main_firmware_state_output(struct spotflow_ota_main_firmware_state* out_state);
static void main_firmware_wake_paused_worker(void);
static void main_firmware_drain_resume_sem(void);
static void wait_while_paused(void);
static int check_user_abort(void);
static int complete_user_fail(struct spotflow_ota_state_action* action,
			      struct spotflow_ota_main_firmware_state* out_state);
static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx);
static int persist_prereboot_attempt(uint64_t attempt_id);
static int persist_snapshot_and_enqueue_results(void);
static int complete_main_firmware_success(const struct spotflow_ota_probation* probation,
					  struct spotflow_ota_state_action* action);
static int complete_main_firmware_rollback(const struct spotflow_ota_probation* probation,
					   struct spotflow_ota_state_action* action);
static bool main_artifact_is_pending(const struct spotflow_ota_probation* probation);

void spotflow_ota_fw_main_reset(void)
{
	k_mutex_lock(&main_firmware_downloader.mutex, K_FOREVER);
	main_firmware_downloader.state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	main_firmware_downloader.cancel_requested = false;
	k_mutex_unlock(&main_firmware_downloader.mutex);
	while (k_sem_take(&main_firmware_downloader.resume_sem, K_NO_WAIT) == 0) {
	}
	user_fail_requested = false;
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

	user_fail_requested = false;

	if (spotflow_ota_state_store_main_firmware_artifact(attempt_id, artifact_index, artifact) <
	    0) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	if (check_user_abort() < 0) {
		return user_fail_requested ? fail_main_firmware() : SPOTFLOW_OTA_RESULT_CANCELED;
	}

	if (spotflow_is_update_canceled()) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD);

	if (check_user_abort() < 0) {
		return user_fail_requested ? fail_main_firmware() : SPOTFLOW_OTA_RESULT_CANCELED;
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

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_DOWNLOADING);

	rc = spotflow_download_artifact(&main_firmware_downloader, &request, download_block_cb,
					&flash_ctx);
	if (rc == -ECANCELED) {
		return user_fail_requested ? fail_main_firmware() : SPOTFLOW_OTA_RESULT_CANCELED;
	}
	if (rc < 0) {
		LOG_ERR("Main firmware download failed: %d", rc);
		return fail_main_firmware();
	}
	if (flash_ctx.write_err != 0) {
		LOG_ERR("Main firmware flash write failed: %d", flash_ctx.write_err);
		return fail_main_firmware();
	}

	if (spotflow_is_update_canceled()) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	if (check_user_abort() < 0) {
		return user_fail_requested ? fail_main_firmware() : SPOTFLOW_OTA_RESULT_CANCELED;
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_UPGRADE);

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

	rc = spotflow_ota_persistence_save_probation(&probation);
	if (rc < 0) {
		LOG_ERR("Failed to persist main firmware probation record: %d", rc);
		return fail_main_firmware();
	}

	if (check_user_abort() < 0) {
		return user_fail_requested ? fail_main_firmware() : SPOTFLOW_OTA_RESULT_CANCELED;
	}

	rc = persist_prereboot_attempt(attempt_id);
	if (rc < 0) {
		LOG_ERR("Failed to persist main firmware attempt before reboot: %d", rc);
		return fail_main_firmware();
	}

	rc = spotflow_ota_platform_request_test_upgrade();
	if (rc < 0) {
		LOG_ERR("Failed to request main firmware test upgrade: %d", rc);
		return fail_main_firmware();
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_REBOOT);

	rc = spotflow_ota_state_finish_main_firmware_prereboot(NULL);
	if (rc < 0) {
		LOG_ERR("Failed to finalize main firmware before reboot: %d", rc);
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
					   bool has_probation,
					   struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		return -EINVAL;
	}

	memset(action, 0, sizeof(*action));

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
		return complete_main_firmware_rollback(probation, action);
	}

	if (identity == SPOTFLOW_OTA_IDENTITY_MISMATCH) {
		LOG_INF("Main firmware rollback detected for OTA attempt %llu ('%s' %s): "
			"reporting failure",
			(unsigned long long)probation->attempt_id, probation->slug,
			probation->version);
		return complete_main_firmware_rollback(probation, action);
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
	return complete_main_firmware_success(probation, action);
}

int spotflow_ota_fw_main_confirm_image(struct spotflow_ota_main_firmware_state* out_state,
				       struct spotflow_ota_state_action* action)
{
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_probation probation;
	bool has_probation;
	int rc;

	if (action == NULL) {
		return -EINVAL;
	}

	memset(action, 0, sizeof(*action));

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt) {
		if (out_state != NULL) {
			*out_state = snapshot.main_firmware_state;
		}

		return -EINVAL;
	}

	if (snapshot.main_firmware_state.phase == SPOTFLOW_OTA_PHASE_NOT_RUNNING &&
	    snapshot.main_firmware_state.result == SPOTFLOW_OTA_RESULT_SUCCEEDED &&
	    spotflow_ota_platform_is_image_confirmed()) {
		if (out_state != NULL) {
			*out_state = snapshot.main_firmware_state;
		}

		return 0;
	}

	rc = spotflow_ota_persistence_load_probation(&probation, &has_probation);
	if (rc < 0) {
		return rc;
	}

	if (has_probation && probation.attempt_id == snapshot.current_attempt_id &&
	    spotflow_ota_identity_compare_probation(probation.expected_build_id) ==
		SPOTFLOW_OTA_IDENTITY_MATCH &&
	    spotflow_ota_platform_is_image_confirmed() &&
	    snapshot.main_firmware_state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		rc = complete_main_firmware_success(&probation, action);
		if (rc < 0) {
			return rc;
		}

		if (out_state != NULL) {
			spotflow_ota_state_get_snapshot(&snapshot);
			*out_state = snapshot.main_firmware_state;
		}

		return 0;
	}

	if (snapshot.main_firmware_state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		LOG_ERR("Main firmware confirmation rejected: has_attempt=%d phase=%d result=%d",
			snapshot.has_current_attempt, snapshot.main_firmware_state.phase,
			snapshot.main_firmware_state.result);
		if (out_state != NULL) {
			*out_state = snapshot.main_firmware_state;
		}

		return -EINVAL;
	}

	if (!has_probation || probation.attempt_id != snapshot.current_attempt_id) {
		LOG_ERR("Main firmware confirmation rejected: has_probation=%d attempt_id=%llu "
			"current_attempt_id=%llu",
			has_probation, (unsigned long long)probation.attempt_id,
			(unsigned long long)snapshot.current_attempt_id);
		if (out_state != NULL) {
			*out_state = snapshot.main_firmware_state;
		}

		return -EINVAL;
	}

	rc = spotflow_ota_platform_confirm_image();
	if (rc < 0) {
		if (out_state != NULL) {
			*out_state = snapshot.main_firmware_state;
		}

		return rc;
	}

	rc = complete_main_firmware_success(&probation, action);
	if (rc < 0) {
		return rc;
	}

	if (out_state != NULL) {
		spotflow_ota_state_get_snapshot(&snapshot);
		*out_state = snapshot.main_firmware_state;
	}

	return 0;
}

int spotflow_ota_fw_main_pause_update(struct spotflow_ota_main_firmware_state* out_state)
{
	struct spotflow_ota_state_snapshot snapshot;
	enum spotflow_downloader_state downloader_state;
	int rc;

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt ||
	    !main_firmware_phase_allows_pause(snapshot.main_firmware_state.phase)) {
		fill_main_firmware_state_output(out_state);
		return -EINVAL;
	}

	if (snapshot.main_firmware_state.is_paused) {
		fill_main_firmware_state_output(out_state);
		return 0;
	}

	rc = spotflow_ota_state_set_main_firmware_paused(true, out_state);
	if (rc < 0) {
		return rc;
	}

	downloader_state = spotflow_get_downloader_state(&main_firmware_downloader);
	if (downloader_state == SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING) {
		rc = spotflow_pause_download(&main_firmware_downloader);
		if (rc < 0) {
			(void)spotflow_ota_state_set_main_firmware_paused(false, NULL);
			fill_main_firmware_state_output(out_state);
			return rc;
		}
	}

	return 0;
}

int spotflow_ota_fw_main_resume_update(struct spotflow_ota_main_firmware_state* out_state)
{
	struct spotflow_ota_state_snapshot snapshot;
	enum spotflow_downloader_state downloader_state;
	int rc;

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt || !snapshot.main_firmware_state.is_paused) {
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

int spotflow_ota_fw_main_fail_update(struct spotflow_ota_main_firmware_state* out_state,
				     struct spotflow_ota_state_action* action)
{
	struct spotflow_ota_state_snapshot snapshot;
	enum spotflow_downloader_state downloader_state;
	int rc;

	if (action == NULL) {
		return -EINVAL;
	}

	memset(action, 0, sizeof(*action));

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt ||
	    !main_firmware_phase_allows_fail(snapshot.main_firmware_state.phase)) {
		fill_main_firmware_state_output(out_state);
		return -EINVAL;
	}

	user_fail_requested = true;
	spotflow_ota_fw_main_cancel_active_download();

	downloader_state = spotflow_get_downloader_state(&main_firmware_downloader);
	if (downloader_state == SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING ||
	    downloader_state == SPOTFLOW_DOWNLOADER_STATE_PAUSED ||
	    downloader_state == SPOTFLOW_DOWNLOADER_STATE_CANCELING) {
		fill_main_firmware_state_output(out_state);
		return 0;
	}

	if (snapshot.main_firmware_state.is_paused) {
		fill_main_firmware_state_output(out_state);
		return 0;
	}

	rc = complete_user_fail(action, out_state);
	if (rc == 0) {
		user_fail_requested = false;
	}

	return rc;
}

void spotflow_ota_fw_main_wake_if_paused(void)
{
	struct spotflow_ota_state_snapshot snapshot;

	spotflow_ota_state_get_snapshot(&snapshot);
	if (snapshot.has_current_attempt && snapshot.main_firmware_state.is_paused) {
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

	LOG_DBG("Main firmware phase -> %s", spotflow_ota_log_phase_name(phase));

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

static bool main_firmware_phase_allows_pause(enum spotflow_ota_phase phase)
{
	switch (phase) {
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
	case SPOTFLOW_OTA_PHASE_PENDING_REBOOT:
		return true;
	default:
		return false;
	}
}

static bool main_firmware_phase_allows_fail(enum spotflow_ota_phase phase)
{
	switch (phase) {
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
		return true;
	default:
		return false;
	}
}

static void fill_main_firmware_state_output(struct spotflow_ota_main_firmware_state* out_state)
{
	struct spotflow_ota_state_snapshot snapshot;

	if (out_state == NULL) {
		return;
	}

	spotflow_ota_state_get_snapshot(&snapshot);
	*out_state = snapshot.main_firmware_state;
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

static void wait_while_paused(void)
{
	struct spotflow_ota_state_snapshot snapshot;

	for (;;) {
		if (user_fail_requested || spotflow_is_update_canceled()) {
			return;
		}

		spotflow_ota_state_get_snapshot(&snapshot);
		if (!snapshot.main_firmware_state.is_paused) {
			return;
		}

		k_sem_take(&main_firmware_resume_sem, K_FOREVER);
	}
}

static int check_user_abort(void)
{
	wait_while_paused();

	if (user_fail_requested || spotflow_is_update_canceled()) {
		return -ECANCELED;
	}

	return 0;
}

static int complete_user_fail(struct spotflow_ota_state_action* action,
			      struct spotflow_ota_main_firmware_state* out_state)
{
	struct spotflow_ota_state_snapshot snapshot;
	size_t artifact_index;
	int rc;

	rc = spotflow_ota_state_get_main_firmware_artifact_index(&artifact_index);
	if (rc < 0) {
		fill_main_firmware_state_output(out_state);
		return rc;
	}

	(void)fail_main_firmware();

	rc = spotflow_ota_state_apply_artifact_result(artifact_index, SPOTFLOW_OTA_RESULT_FAILED,
						      action);
	if (rc < 0) {
		fill_main_firmware_state_output(out_state);
		return rc;
	}

	rc = persist_snapshot_and_enqueue_results();
	if (rc < 0) {
		fill_main_firmware_state_output(out_state);
		return rc;
	}

	if (out_state != NULL) {
		spotflow_ota_state_get_snapshot(&snapshot);
		*out_state = snapshot.main_firmware_state;
	}

	return 0;
}

static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx)
{
	ARG_UNUSED(downloader);

	struct main_firmware_flash_ctx* ctx = callback_ctx;

	if (ctx->write_err != 0) {
		return;
	}

	if (block->data_len == 0 && !block->is_last) {
		return;
	}

	ctx->write_err =
	    spotflow_ota_platform_write_image_block(block->data, block->data_len, block->is_last);
}

static int persist_prereboot_attempt(uint64_t attempt_id)
{
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_persisted_attempt attempt;

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt || snapshot.current_attempt_id != attempt_id) {
		return -EINVAL;
	}

	attempt = (struct spotflow_ota_persisted_attempt){
		.attempt_id = snapshot.current_attempt_id,
		.artifact_count = snapshot.artifact_count,
		.actionable_cancellation = snapshot.actionable_cancellation,
		.has_attempt_error = snapshot.has_attempt_error,
		.attempt_error = snapshot.attempt_error,
	};
	memcpy(attempt.artifact_results, snapshot.artifact_results,
	       sizeof(attempt.artifact_results));

	return spotflow_ota_persistence_save_attempt(&attempt);
}

static int persist_snapshot_and_enqueue_results(void)
{
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_persisted_attempt attempt;
	int rc;

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt || snapshot.has_attempt_error) {
		return -EINVAL;
	}

	attempt = (struct spotflow_ota_persisted_attempt){
		.attempt_id = snapshot.current_attempt_id,
		.artifact_count = snapshot.artifact_count,
		.actionable_cancellation = snapshot.actionable_cancellation,
	};
	memcpy(attempt.artifact_results, snapshot.artifact_results,
	       sizeof(attempt.artifact_results));

	rc = spotflow_ota_persistence_save_attempt(&attempt);
	if (rc < 0) {
		LOG_ERR("Failed to persist main firmware attempt results: %d", rc);
		return rc;
	}

	rc = spotflow_ota_net_prepare_results(snapshot.current_attempt_id,
					      snapshot.artifact_results, snapshot.artifact_count);
	if (rc < 0) {
		LOG_ERR("Failed to queue main firmware attempt results: %d", rc);
	}

	return rc;
}

static int complete_main_firmware_success(const struct spotflow_ota_probation* probation,
					  struct spotflow_ota_state_action* action)
{
	struct spotflow_ota_main_firmware_state state;
	int rc;

	LOG_INF("Main firmware update succeeded for OTA attempt %llu ('%s' %s)",
		(unsigned long long)probation->attempt_id, probation->slug, probation->version);

	rc = spotflow_ota_persistence_save_installed_version(probation->slug, probation->version);
	if (rc < 0) {
		LOG_ERR("Failed to persist installed main firmware version: %d", rc);
		return rc;
	}

	rc = spotflow_ota_state_apply_artifact_result(probation->artifact_index,
						      SPOTFLOW_OTA_RESULT_SUCCEEDED, action);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_ota_state_set_main_firmware_result(SPOTFLOW_OTA_RESULT_SUCCEEDED, &state);
	if (rc < 0) {
		return rc;
	}

	notify_main_firmware_state(&state);

	rc = spotflow_ota_persistence_clear_probation();
	if (rc < 0) {
		LOG_ERR("Failed to clear main firmware probation record: %d", rc);
		return rc;
	}

	spotflow_ota_state_clear_main_firmware_awaiting_reboot();

	rc = persist_snapshot_and_enqueue_results();
	if (rc < 0) {
		return rc;
	}

	if (!action->wake_worker) {
		struct spotflow_ota_state_snapshot snapshot;

		spotflow_ota_state_get_snapshot(&snapshot);
		for (size_t i = 0; i < snapshot.artifact_count; i++) {
			if (snapshot.artifact_results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
				action->wake_worker = true;
				break;
			}
		}
	}

	return 0;
}

static int complete_main_firmware_rollback(const struct spotflow_ota_probation* probation,
					   struct spotflow_ota_state_action* action)
{
	struct spotflow_ota_main_firmware_state state;
	int rc;

	rc = spotflow_ota_state_set_main_firmware_result(SPOTFLOW_OTA_RESULT_FAILED, &state);
	if (rc < 0) {
		return rc;
	}

	notify_main_firmware_state(&state);

	rc = spotflow_ota_state_apply_artifact_result(probation->artifact_index,
						      SPOTFLOW_OTA_RESULT_FAILED, action);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_ota_persistence_clear_probation();
	if (rc < 0) {
		LOG_ERR("Failed to clear main firmware probation record after rollback: %d", rc);
		return rc;
	}

	spotflow_ota_state_clear_main_firmware_awaiting_reboot();

	return persist_snapshot_and_enqueue_results();
}

static bool main_artifact_is_pending(const struct spotflow_ota_probation* probation)
{
	struct spotflow_ota_state_snapshot snapshot;

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt || snapshot.current_attempt_id != probation->attempt_id) {
		return false;
	}

	if (probation->artifact_index >= snapshot.artifact_count) {
		return false;
	}

	return snapshot.artifact_results[probation->artifact_index] == SPOTFLOW_OTA_RESULT_PENDING;
}
