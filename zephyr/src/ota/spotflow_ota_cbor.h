#ifndef SPOTFLOW_OTA_CBOR_H
#define SPOTFLOW_OTA_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_ota_cbor_msg_type {
	SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS = 0x06,
	SPOTFLOW_OTA_CBOR_MSG_CANCEL_UPDATE = 0x07,
	SPOTFLOW_OTA_CBOR_MSG_REPORT_UPDATE_RESULTS = 0x08,
};

struct spotflow_ota_cbor_c2d_msg {
	enum spotflow_ota_cbor_msg_type type;
	uint64_t attempt_id;
	union {
		struct spotflow_ota_update_msg update;
	} payload;
};

struct spotflow_ota_cbor_decode_status {
	bool has_trustworthy_attempt_id;
	uint64_t attempt_id;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
};

struct spotflow_ota_cbor_update_results {
	uint64_t attempt_id;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	size_t succeeded_count;
	uint32_t succeeded[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	size_t failed_count;
	uint32_t failed[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	size_t canceled_count;
	uint32_t canceled[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
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
