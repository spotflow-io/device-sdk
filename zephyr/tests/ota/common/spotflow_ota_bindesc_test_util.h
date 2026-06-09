#ifndef SPOTFLOW_OTA_BINDESC_TEST_UTIL_H
#define SPOTFLOW_OTA_BINDESC_TEST_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "spotflow_build_id.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t spotflow_ota_test_bindesc_write_build_id(uint8_t* buffer, size_t buffer_size, size_t offset,
						const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

void spotflow_ota_test_set_running_build_id(const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

void spotflow_ota_test_clear_running_build_id(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_BINDESC_TEST_UTIL_H */
