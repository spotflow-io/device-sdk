#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <spotflow/ota.h>

#include "net.h"

LOG_MODULE_REGISTER(ota_sample, LOG_LEVEL_INF);

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

static void confirm_unconfirmed_main_firmware(void)
{
	struct spotflow_ota_main_firmware_state fw_state;
	int ret = spotflow_get_main_firmware_update_state(&fw_state);

	if (ret < 0) {
		LOG_ERR("Failed to query main firmware state: %d", ret);
		return;
	}

	if (fw_state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
		LOG_INF("Main firmware state: phase=%s result=%d", ota_phase_name(fw_state.phase),
			fw_state.result);
		return;
	}

	LOG_INF("Unconfirmed main firmware detected (phase=%s), confirming via Spotflow OTA",
		ota_phase_name(fw_state.phase));

	ret = spotflow_confirm_main_firmware_image(&fw_state);
	if (ret < 0) {
		LOG_ERR("Failed to confirm main firmware image: %d (phase=%s result=%d)", ret,
			ota_phase_name(fw_state.phase), fw_state.result);
		return;
	}

	LOG_INF("Main firmware confirmed successfully (phase=%s result=%d)",
		ota_phase_name(fw_state.phase), fw_state.result);
}

void spotflow_on_main_firmware_update_progressed(
    const struct spotflow_ota_main_firmware_state* state)
{
	if (state == NULL) {
		return;
	}

	LOG_INF("Main firmware progress: phase=%s paused=%d result=%d",
		ota_phase_name(state->phase), state->is_paused, state->result);
}

enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info)
{
	if (info == NULL) {
		LOG_ERR("Non-main firmware callback received NULL info");
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	LOG_INF("Non-main firmware update requested: slug='%s' version=%s", info->slug,
		info->version);

	if (spotflow_is_update_canceled()) {
		LOG_INF("Non-main firmware update canceled before handling");
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	/*
	 * Implement delegated firmware updates here. Use info->download_request with
	 * spotflow_download_artifact() to stream the artifact, then return a terminal
	 * spotflow_ota_result. This sample does not handle non-main artifacts.
	 */
	LOG_WRN("Non-main firmware updates are not implemented in this sample");
	return SPOTFLOW_OTA_RESULT_FAILED;
}

void spotflow_on_update_canceled(void)
{
	LOG_INF("OTA update canceled by the cloud");
}

int main(void)
{
	LOG_INF("Starting Spotflow OTA sample");

	confirm_unconfirmed_main_firmware();

	/* Wait for the network device to initialize. */
	k_sleep(K_SECONDS(1));

	spotflow_sample_net_init();

	LOG_INF("Ready to receive OTA updates");

	return 0;
}
