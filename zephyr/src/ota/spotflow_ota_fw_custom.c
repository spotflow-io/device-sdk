#include <errno.h>

#include <zephyr/kernel.h>

#include "ota/spotflow_ota_fw_custom.h"
#include "ota/spotflow_ota_state.h"

static void canceled_work_handler(struct k_work* work);

static K_WORK_DEFINE(canceled_work, canceled_work_handler);

enum spotflow_ota_result
spotflow_ota_fw_custom_process_artifact(uint64_t attempt_id,
					const struct spotflow_ota_artifact* artifact)
{
	if (artifact == NULL) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	struct spotflow_download_request request = {
		.url = artifact->url,
		.secret = artifact->secret,
	};
	struct spotflow_firmware_info info = {
		.attempt_id = attempt_id,
		.slug = artifact->slug,
		.is_main = artifact->is_main,
		.download_request = &request,
		.version = artifact->version,
	};

	return spotflow_on_handle_firmware_update(&info);
}

void spotflow_ota_fw_custom_notify_canceled(void)
{
	k_work_submit(&canceled_work);
}

enum spotflow_ota_result __weak
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info)
{
	ARG_UNUSED(info);
	return SPOTFLOW_OTA_RESULT_FAILED;
}

void __weak spotflow_on_update_canceled(void)
{
}

void __weak spotflow_on_main_firmware_update_progressed(
	const struct spotflow_ota_main_firmware_state* state)
{
	ARG_UNUSED(state);
}

static void canceled_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);
	spotflow_on_update_canceled();
}
