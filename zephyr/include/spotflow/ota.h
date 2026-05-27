#ifndef SPOTFLOW_OTA_PUBLIC_H
#define SPOTFLOW_OTA_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>

#include "downloader.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_firmware_info {
	uint64_t attempt_id;
	const char *slug;
	bool is_main;
	const struct spotflow_download_request *download_request;
	const char *version;
};

enum spotflow_ota_result {
	SPOTFLOW_OTA_RESULT_PENDING,
	SPOTFLOW_OTA_RESULT_SUCCEEDED,
	SPOTFLOW_OTA_RESULT_FAILED,
	SPOTFLOW_OTA_RESULT_CANCELED,
};

enum spotflow_ota_phase {
	SPOTFLOW_OTA_PHASE_NOT_RUNNING,
	SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD,
	SPOTFLOW_OTA_PHASE_DOWNLOADING,
	SPOTFLOW_OTA_PHASE_PENDING_UPGRADE,
	SPOTFLOW_OTA_PHASE_PENDING_REBOOT,
	SPOTFLOW_OTA_PHASE_UNCONFIRMED,
};

struct spotflow_ota_main_firmware_state {
	enum spotflow_ota_phase phase;
	bool is_paused;
	enum spotflow_ota_result result;
};

enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info *info);

void spotflow_on_update_canceled(void);

bool spotflow_is_update_canceled(void);

void spotflow_on_main_firmware_update_progressed(
	const struct spotflow_ota_main_firmware_state *state);

int spotflow_get_main_firmware_update_state(struct spotflow_ota_main_firmware_state *state);

int spotflow_get_main_firmware_update_info(struct spotflow_firmware_info *info);

int spotflow_pause_main_firmware_update(struct spotflow_ota_main_firmware_state *state);

int spotflow_resume_main_firmware_update(struct spotflow_ota_main_firmware_state *state);

int spotflow_fail_main_firmware_update(struct spotflow_ota_main_firmware_state *state);

int spotflow_confirm_main_firmware_image(struct spotflow_ota_main_firmware_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PUBLIC_H */
