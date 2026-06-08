#ifndef SPOTFLOW_OTA_PLATFORM_H
#define SPOTFLOW_OTA_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bindesc.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_ota_platform_request_test_upgrade(void);

int spotflow_ota_platform_confirm_image(void);

bool spotflow_ota_platform_is_image_confirmed(void);

void spotflow_ota_platform_reboot(void);

int spotflow_ota_platform_read_upload_slot(size_t offset, uint8_t* dst, size_t len);

int spotflow_ota_platform_get_upload_image_info(size_t* image_start, size_t* image_size);

int spotflow_ota_platform_bindesc_open_upload(struct bindesc_handle* handle,
					      size_t partition_offset);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PLATFORM_H */
