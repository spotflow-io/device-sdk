#ifndef SPOTFLOW_OTA_PLATFORM_FAKE_H
#define SPOTFLOW_OTA_PLATFORM_FAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_PLATFORM_FAKE_UPLOAD_SLOT_SIZE 512

struct spotflow_ota_platform_fake {
	uint32_t upgrade_request_count;
	uint32_t confirm_count;
	uint32_t reboot_count;
	int upgrade_request_result;
	int confirm_result;
	bool image_confirmed;
	int read_result;
	int image_info_result;
	int begin_write_result;
	int write_result;
	/** Fail writes once this many bytes have been written successfully. */
	size_t write_fail_after_bytes;
	size_t upload_image_start;
	size_t upload_image_size;
	uint8_t upload_slot[SPOTFLOW_OTA_PLATFORM_FAKE_UPLOAD_SLOT_SIZE];
};

void spotflow_ota_platform_fake_reset(struct spotflow_ota_platform_fake* fake);

struct spotflow_ota_platform_fake* spotflow_ota_platform_fake_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PLATFORM_FAKE_H */
