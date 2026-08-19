#ifndef SPOTFLOW_OTA_RECORDS_CBOR_H
#define SPOTFLOW_OTA_RECORDS_CBOR_H

#include <stddef.h>
#include <stdint.h>

#include "ota/persistence/spotflow_ota_records.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_RECORDS_CBOR_SCHEMA_VERSION 1

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
