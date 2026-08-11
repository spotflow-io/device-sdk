#ifndef SPOTFLOW_OTA_RECORDS_H
#define SPOTFLOW_OTA_RECORDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "spotflow_build_id.h"
#include "ota/core/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_persisted_attempt {
	uint64_t attempt_id;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	size_t artifact_count;
	bool actionable_cancellation;
	enum spotflow_ota_result artifact_results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
};

struct spotflow_ota_probation {
	uint64_t attempt_id;
	uint32_t artifact_index;
	char slug[SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH + 1];
	char version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH];
};

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_RECORDS_H */
