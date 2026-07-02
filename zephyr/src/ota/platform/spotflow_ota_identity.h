#ifndef SPOTFLOW_OTA_IDENTITY_H
#define SPOTFLOW_OTA_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#include "spotflow_build_id.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_identity_cmp {
	SPOTFLOW_OTA_IDENTITY_MATCH = 0,
	SPOTFLOW_OTA_IDENTITY_MISMATCH,
	SPOTFLOW_OTA_IDENTITY_UNAVAILABLE,
};

int spotflow_ota_identity_get_running_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

int spotflow_ota_identity_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

enum spotflow_ota_identity_cmp
spotflow_ota_identity_compare_probation(const uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_IDENTITY_H */
