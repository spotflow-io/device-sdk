#ifndef SPOTFLOW_OTA_FW_MAIN_COMMON_H
#define SPOTFLOW_OTA_FW_MAIN_COMMON_H

#include "ota/firmware/spotflow_ota_fw_main.h"
#include "ota/downloader/spotflow_ota_downloader.h"

/* Internal download controls used by the main-firmware coordinator. */
extern struct spotflow_downloader main_firmware_downloader;
void notify_main_firmware_state(const struct spotflow_ota_main_firmware_state* state);
enum spotflow_ota_result fail_main_firmware(const struct spotflow_ota_operation_token* token);
int main_firmware_control_checkpoint(void);
enum spotflow_ota_result
interrupted_main_firmware_result(const struct spotflow_ota_operation_token* token);
int begin_main_firmware_installation(const struct spotflow_ota_operation_token* token);
int begin_main_firmware_reboot(const struct spotflow_ota_operation_token* token);
void download_started_cb(struct spotflow_downloader* downloader, void* callback_ctx);

#endif /* SPOTFLOW_OTA_FW_MAIN_COMMON_H */
