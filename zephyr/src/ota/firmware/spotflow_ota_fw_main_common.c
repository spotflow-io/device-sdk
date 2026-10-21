#include "ota/firmware/spotflow_ota_fw_main_common.h"
#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/core/spotflow_ota_log.h"

#include <errno.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

SPOTFLOW_DEFINE_DOWNLOADER(main_firmware_downloader);

static K_SEM_DEFINE(main_firmware_resume_sem, 0, 1);

static void fill_main_firmware_state_output(struct spotflow_ota_main_firmware_state* out_state);
static void main_firmware_wake_paused_worker(void);
static void main_firmware_drain_resume_sem(void);
static void wait_while_paused(bool honor_interruptions);
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

void notify_main_firmware_state(const struct spotflow_ota_main_firmware_state* state)
{
	if (state == NULL) {
		return;
	}

	spotflow_on_main_firmware_update_progressed(state);
}

enum spotflow_ota_result fail_main_firmware(const struct spotflow_ota_operation_token* token)
{
	struct spotflow_ota_main_firmware_state state;

	if (spotflow_ota_state_fail_main_firmware(token, &state) == 0) {
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

int main_firmware_control_checkpoint(void)
{
	wait_while_paused(true);

	if (spotflow_ota_state_is_main_firmware_abort_requested() ||
	    spotflow_is_update_canceled()) {
		return -ECANCELED;
	}

	return 0;
}

enum spotflow_ota_result
interrupted_main_firmware_result(const struct spotflow_ota_operation_token* token)
{
	if (spotflow_ota_state_is_main_firmware_abort_requested()) {
		return fail_main_firmware(token);
	}

	return SPOTFLOW_OTA_RESULT_CANCELED;
}

int begin_main_firmware_upgrade_commit(const struct spotflow_ota_operation_token* token)
{
	for (;;) {
		int rc = main_firmware_control_checkpoint();

		if (rc < 0) {
			return rc;
		}

		rc = spotflow_ota_state_begin_main_firmware_upgrade_commit(token);
		if (rc != -EAGAIN) {
			return rc;
		}
	}
}

int begin_main_firmware_reboot(const struct spotflow_ota_operation_token* token)
{
	for (;;) {
		int rc;

		wait_while_paused(false);

		rc = spotflow_ota_state_begin_main_firmware_reboot(token);
		if (rc != -EAGAIN) {
			return rc;
		}
	}
}

void download_started_cb(struct spotflow_downloader* downloader, void* callback_ctx)
{
	const struct spotflow_ota_operation_token* token = callback_ctx;
	struct spotflow_ota_main_firmware_state state;
	if (spotflow_ota_state_main_firmware_download_started(token, &state) < 0) {
		(void)spotflow_cancel_download(downloader);
		return;
	}
	LOG_INF("Main firmware phase -> %s", spotflow_ota_log_phase_name(state.phase));
	notify_main_firmware_state(&state);

	if (spotflow_ota_state_is_main_firmware_abort_requested() ||
	    spotflow_is_update_canceled()) {
		(void)spotflow_cancel_download(downloader);
	} else if (state.is_paused) {
		(void)spotflow_pause_download(downloader);
	}
}
