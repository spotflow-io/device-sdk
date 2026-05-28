#ifndef SPOTFLOW_OTA_CBOR_H
#define SPOTFLOW_OTA_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS
#define SPOTFLOW_OTA_MAX_ARTIFACTS CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS
#else
#define SPOTFLOW_OTA_MAX_ARTIFACTS 4
#endif

#define SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH 32
#define SPOTFLOW_OTA_ARTIFACT_URL_PROTOCOL_MAX_LENGTH 117
#define SPOTFLOW_OTA_ARTIFACT_URL_BUFFER_LENGTH 128
#define SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH 24
#define SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH 64

enum spotflow_ota_cbor_msg_type {
	SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS = 0x06,
	SPOTFLOW_OTA_CBOR_MSG_CANCEL_UPDATE = 0x07,
	SPOTFLOW_OTA_CBOR_MSG_REPORT_UPDATE_RESULTS = 0x08,
};

enum spotflow_ota_cbor_artifact_type {
	SPOTFLOW_OTA_CBOR_ARTIFACT_TYPE_FIRMWARE = 0x00,
};

enum spotflow_ota_cbor_attempt_error {
	SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_UNKNOWN = 0,
	SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_ARTIFACT_COUNT_EXCEEDED = 1,
	SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE = 2,
	SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE = 3,
};

struct spotflow_ota_cbor_artifact {
	enum spotflow_ota_cbor_artifact_type type;
	char slug[SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH + 1];
	bool is_main;
	char url[SPOTFLOW_OTA_ARTIFACT_URL_BUFFER_LENGTH + 1];
	char secret[SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH + 1];
	char version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
};

struct spotflow_ota_cbor_update_artifacts {
	bool is_canceled;
	size_t artifact_count;
	struct spotflow_ota_cbor_artifact artifacts[SPOTFLOW_OTA_MAX_ARTIFACTS];
};

struct spotflow_ota_cbor_c2d_msg {
	enum spotflow_ota_cbor_msg_type type;
	uint64_t attempt_id;
	union {
		struct spotflow_ota_cbor_update_artifacts update;
	} payload;
};

struct spotflow_ota_cbor_decode_status {
	bool has_trustworthy_attempt_id;
	uint64_t attempt_id;
	enum spotflow_ota_cbor_attempt_error attempt_error;
};

struct spotflow_ota_cbor_update_results {
	uint64_t attempt_id;
	bool has_attempt_error;
	enum spotflow_ota_cbor_attempt_error attempt_error;
	size_t succeeded_count;
	uint32_t succeeded[SPOTFLOW_OTA_MAX_ARTIFACTS];
	size_t failed_count;
	uint32_t failed[SPOTFLOW_OTA_MAX_ARTIFACTS];
	size_t canceled_count;
	uint32_t canceled[SPOTFLOW_OTA_MAX_ARTIFACTS];
};

int spotflow_ota_cbor_decode_c2d(const uint8_t* payload, size_t len,
				 struct spotflow_ota_cbor_c2d_msg* msg,
				 struct spotflow_ota_cbor_decode_status* status);

int spotflow_ota_cbor_encode_update_results(const struct spotflow_ota_cbor_update_results* msg,
					    uint8_t* buffer, size_t len, size_t* encoded_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_CBOR_H */
