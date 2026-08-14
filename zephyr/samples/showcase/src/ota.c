#include "showcase.h"

#include <spotflow/ota.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(showcase_ota, LOG_LEVEL_INF);

#define DELEGATED_PAUSE_AFTER_BYTES 4096
#define DEMO_PAUSE_DURATION K_SECONDS(3)

static SPOTFLOW_DEFINE_DOWNLOADER(delegated_downloader);

struct delegated_download_context {
	size_t received;
	uint32_t checksum;
	bool valid;
	bool saw_last;
	bool pause_demonstrated;
};

static struct k_work_delayable resume_main_work;
static struct k_work_delayable resume_delegated_work;
static atomic_t main_pause_demonstrated;
static atomic_t delegated_update_active;

static const char* ota_phase_name(enum spotflow_ota_phase phase)
{
	switch (phase) {
	case SPOTFLOW_OTA_PHASE_NOT_RUNNING:
		return "NOT_RUNNING";
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
		return "PENDING_DOWNLOAD";
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
		return "DOWNLOADING";
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
		return "PENDING_UPGRADE";
	case SPOTFLOW_OTA_PHASE_PENDING_REBOOT:
		return "PENDING_REBOOT";
	case SPOTFLOW_OTA_PHASE_UNCONFIRMED:
		return "UNCONFIRMED";
	default:
		return "UNKNOWN";
	}
}

static void resume_main_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	struct spotflow_ota_main_firmware_state state;
	int rc = spotflow_resume_main_firmware_update(&state);

	if (rc < 0) {
		LOG_WRN("Automatic main-firmware resume was not applicable: %d", rc);
		return;
	}

	LOG_INF("Resumed main-firmware update in phase %s", ota_phase_name(state.phase));
}

static void resume_delegated_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	int rc = spotflow_resume_download(&delegated_downloader);

	if (rc < 0) {
		LOG_WRN("Delegated download resume was not applicable: %d", rc);
	} else {
		LOG_INF("Resumed delegated artifact download");
	}
}

static void receive_delegated_block(const struct spotflow_artifact_block* block,
				    struct spotflow_downloader* downloader, void* callback_ctx)
{
	struct delegated_download_context* context = callback_ctx;

	if (block->offset != context->received) {
		context->valid = false;
		(void)spotflow_cancel_download(downloader);
		return;
	}

	for (size_t i = 0; i < block->data_len; ++i) {
		context->checksum = (context->checksum * 16777619U) ^ block->data[i];
	}
	context->received += block->data_len;
	context->saw_last = block->is_last;

	if (!context->pause_demonstrated && context->received >= DELEGATED_PAUSE_AFTER_BYTES &&
	    !block->is_last) {
		int rc = spotflow_pause_download(downloader);

		if (rc == 0) {
			context->pause_demonstrated = true;
			LOG_INF("Paused delegated download after %zu bytes", context->received);
			(void)k_work_reschedule(&resume_delegated_work, DEMO_PAUSE_DURATION);
		}
	}
}

void spotflow_on_main_firmware_update_progressed(
	const struct spotflow_ota_main_firmware_state* state)
{
	if (state == NULL) {
		return;
	}

	LOG_INF("Main firmware: phase=%s paused=%d result=%d", ota_phase_name(state->phase),
		state->is_paused, state->result);

	if (state->phase == SPOTFLOW_OTA_PHASE_NOT_RUNNING) {
		atomic_clear(&main_pause_demonstrated);
		return;
	}

	if (state->phase != SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD || state->is_paused ||
	    !atomic_cas(&main_pause_demonstrated, 0, 1)) {
		return;
	}

	struct spotflow_firmware_info info;
	struct spotflow_download_request request;
	int rc = spotflow_get_main_firmware_update_info(&info, &request);

	if (rc == 0) {
		LOG_INF("Main artifact '%s' version '%s' is ready", info.slug, info.version);
	}

	struct spotflow_ota_main_firmware_state paused_state;

	rc = spotflow_pause_main_firmware_update(&paused_state);
	if (rc < 0) {
		LOG_WRN("Failed to demonstrate main-firmware pause: %d", rc);
		return;
	}

	LOG_INF("Paused main-firmware update for three seconds");
	(void)k_work_reschedule(&resume_main_work, DEMO_PAUSE_DURATION);
}

enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info)
{
	if (info == NULL || info->download_request == NULL) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	LOG_INF("Simulating delegated update for '%s', version '%s'", info->slug, info->version);
	struct delegated_download_context context = {
		.checksum = 2166136261U,
		.valid = true,
	};

	atomic_set(&delegated_update_active, 1);
	int rc = spotflow_download_artifact(&delegated_downloader, info->download_request,
					    receive_delegated_block, &context);
	atomic_clear(&delegated_update_active);

	if (spotflow_is_update_canceled() || rc == -ECANCELED) {
		LOG_INF("Delegated update canceled after %zu bytes", context.received);
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}
	if (rc < 0 || !context.valid || !context.saw_last) {
		LOG_ERR("Delegated download failed: %d", rc);
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	/* A real product would verify, install, and persist the external firmware here. */
	LOG_INF("Simulated delegated install succeeded: bytes=%zu checksum=%08x", context.received,
		context.checksum);
	return SPOTFLOW_OTA_RESULT_SUCCEEDED;
}

void spotflow_on_update_canceled(void)
{
	if (atomic_get(&delegated_update_active) &&
	    spotflow_get_downloader_state(&delegated_downloader) !=
		    SPOTFLOW_DOWNLOADER_STATE_INACTIVE) {
		(void)spotflow_cancel_download(&delegated_downloader);
	}
}

int showcase_ota_init(void)
{
	k_work_init_delayable(&resume_main_work, resume_main_work_handler);
	k_work_init_delayable(&resume_delegated_work, resume_delegated_work_handler);

	struct spotflow_ota_main_firmware_state state;
	int rc = spotflow_get_main_firmware_update_state(&state);

	if (rc < 0) {
		return rc;
	}

	LOG_INF("Initial main-firmware state: phase=%s result=%d", ota_phase_name(state.phase),
		state.result);
	if (state.phase == SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		LOG_WRN("Running image is unconfirmed; short-press the button to accept it");
	}

	return 0;
}

void showcase_ota_handle_short_press(void)
{
	struct spotflow_ota_main_firmware_state state;
	int rc = spotflow_get_main_firmware_update_state(&state);

	if (rc < 0) {
		LOG_ERR("Failed to read OTA state: %d", rc);
		return;
	}
	if (state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		LOG_INF("Short press ignored: no unconfirmed main image");
		return;
	}

	rc = spotflow_confirm_main_firmware_image(&state);
	if (rc < 0) {
		LOG_ERR("Failed to confirm the main image: %d", rc);
	} else {
		LOG_INF("Confirmed the running main image");
	}
}

bool showcase_ota_handle_long_press(void)
{
	if (atomic_get(&delegated_update_active)) {
		int rc = spotflow_cancel_download(&delegated_downloader);

		LOG_INF("Long press requested delegated OTA cancellation: %d", rc);
		return true;
	}

	struct spotflow_ota_main_firmware_state state;
	int rc = spotflow_get_main_firmware_update_state(&state);

	if (rc < 0) {
		LOG_ERR("Failed to read OTA state: %d", rc);
		return true;
	}

	switch (state.phase) {
	case SPOTFLOW_OTA_PHASE_NOT_RUNNING:
		return false;
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
		rc = spotflow_abort_main_firmware_update(&state);
		LOG_INF("Long press requested main OTA abort: %d", rc);
		return true;
	case SPOTFLOW_OTA_PHASE_PENDING_REBOOT:
		LOG_WRN("The test upgrade is committed and can no longer be aborted");
		return true;
	case SPOTFLOW_OTA_PHASE_UNCONFIRMED:
		LOG_WRN("Rejecting the unconfirmed image; MCUboot will roll it back");
		k_sleep(K_MSEC(100));
		sys_reboot(SYS_REBOOT_COLD);
		return true;
	default:
		return true;
	}
}
