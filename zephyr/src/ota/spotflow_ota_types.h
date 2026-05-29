#ifndef SPOTFLOW_OTA_TYPES_H
#define SPOTFLOW_OTA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota_cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_attempt_error {
	SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_UNKNOWN_ERROR,
	SPOTFLOW_OTA_ATTEMPT_ERROR_ARTIFACT_COUNT_EXCEEDED =
	    SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_ARTIFACT_COUNT_EXCEEDED,
	SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE =
	    SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE,
	SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE =
	    SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
};

struct spotflow_ota_artifact {
	bool is_main;
	char slug[SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH + 1];
	char url[SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH + 1];
	char secret[SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH + 1];
	char version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	struct spotflow_download_request download_request;
};

struct spotflow_ota_update_msg {
	uint64_t attempt_id;
	bool is_canceled;
	size_t artifact_count;
	struct spotflow_ota_artifact artifacts[SPOTFLOW_OTA_MAX_ARTIFACTS];
};

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_TYPES_H */
