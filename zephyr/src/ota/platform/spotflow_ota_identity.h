#ifndef SPOTFLOW_OTA_IDENTITY_H
#define SPOTFLOW_OTA_IDENTITY_H

#include "spotflow_build_id.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of comparing probation identity with the running executable. */
enum spotflow_ota_identity_cmp {
	SPOTFLOW_OTA_IDENTITY_MATCH = 0,
	SPOTFLOW_OTA_IDENTITY_MISMATCH,
	SPOTFLOW_OTA_IDENTITY_UNAVAILABLE,
};

/**
 * @brief Copy the running executable's identity.
 * @param build_id Receives the embedded build ID.
 * @return Zero on success, negative errno if unavailable or invalid.
 */
int spotflow_ota_identity_get_running_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

/**
 * @brief Compare the running executable with a saved candidate identity.
 * @param expected_build_id Build ID from the probation record.
 * @return Match, mismatch, or unavailable when the identity cannot be read.
 */
enum spotflow_ota_identity_cmp
spotflow_ota_identity_compare_probation(const uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_IDENTITY_H */
