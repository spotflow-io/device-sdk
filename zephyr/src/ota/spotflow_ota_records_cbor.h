#ifndef SPOTFLOW_OTA_RECORDS_CBOR_H
#define SPOTFLOW_OTA_RECORDS_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_RECORDS_CBOR_SCHEMA_VERSION 1
#define SPOTFLOW_OTA_BUILD_ID_MAX_LENGTH 20

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
	size_t expected_build_id_len;
	uint8_t expected_build_id[SPOTFLOW_OTA_BUILD_ID_MAX_LENGTH];
};

int spotflow_ota_records_cbor_encode_attempt(const struct spotflow_ota_persisted_attempt* attempt,
					     uint8_t* buffer, size_t len, size_t* encoded_len);

int spotflow_ota_records_cbor_decode_attempt(const uint8_t* payload, size_t len,
					     struct spotflow_ota_persisted_attempt* attempt);

int spotflow_ota_records_cbor_encode_probation(const struct spotflow_ota_probation* probation,
					       uint8_t* buffer, size_t len, size_t* encoded_len);

int spotflow_ota_records_cbor_decode_probation(const uint8_t* payload, size_t len,
					       struct spotflow_ota_probation* probation);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_RECORDS_CBOR_H */
