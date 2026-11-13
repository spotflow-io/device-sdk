#ifndef SPOTFLOW_OTA_BLE_PROTOCOL_H
#define SPOTFLOW_OTA_BLE_PROTOCOL_H

#include "ota/core/spotflow_ota_limits.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPOTFLOW_OTA_BLE_RESPONSE_FLAG 0x80
#define SPOTFLOW_OTA_BLE_HEADER_SIZE 9
#define SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE 10
/* Operation (1), transfer ID (4), request ID (4), status (1), and
 * artifact size or block offset (4). BLE fragmentation headers are separate.
 */
#define SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE 14
#define SPOTFLOW_OTA_BLE_OPEN_HEADER_SIZE 12
#define SPOTFLOW_OTA_BLE_OPEN_MAX_SIZE                                              \
	(SPOTFLOW_OTA_BLE_OPEN_HEADER_SIZE + SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH + \
	 SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH)
#define SPOTFLOW_OTA_BLE_READ_SIZE 15
#define SPOTFLOW_OTA_BLE_CLOSE_SIZE 14

enum spotflow_ota_ble_operation {
	SPOTFLOW_OTA_BLE_OPEN = 1,
	SPOTFLOW_OTA_BLE_READ = 2,
	SPOTFLOW_OTA_BLE_CLOSE = 3,
};

enum spotflow_ota_ble_status {
	SPOTFLOW_OTA_BLE_STATUS_OK = 0,
	SPOTFLOW_OTA_BLE_STATUS_INVALID_REQUEST = 1,
	SPOTFLOW_OTA_BLE_STATUS_ACCESS_DENIED = 2,
	SPOTFLOW_OTA_BLE_STATUS_TRANSIENT_FAILURE = 3,
	SPOTFLOW_OTA_BLE_STATUS_ARTIFACT_TOO_LARGE = 4,
};

enum spotflow_ota_ble_close_reason {
	SPOTFLOW_OTA_BLE_CLOSE_CONSUMED = 0,
	SPOTFLOW_OTA_BLE_CLOSE_ABORTED = 1,
};

struct spotflow_ota_ble_header {
	uint8_t operation;
	uint32_t transfer_id;
	uint32_t request_id;
};

struct spotflow_ota_ble_response {
	struct spotflow_ota_ble_header header;
	uint8_t status;
	/* Artifact size for OPEN, absolute block offset for READ. */
	uint32_t position;
	const uint8_t* data;
	size_t data_len;
};

/**
 * @brief Decode the common header
 * @param payload Encoded message bytes.
 * @param len Number of encoded bytes.
 * @param header Common operation and correlation fields.
 * @return 0, or -EINVAL for missing/short input.
 */
int spotflow_ota_ble_decode_header(const uint8_t* payload, size_t len,
				   struct spotflow_ota_ble_header* header);
/**
 * @brief Encode OPEN
 * @param buffer Output message buffer.
 * @param capacity Output buffer capacity in bytes.
 * @param header Common operation and correlation fields.
 * @param url Artifact URL.
 * @param secret Per-artifact download credential; never logged.
 * @return its length, -EINVAL for missing arguments, or -EMSGSIZE.
 */
int spotflow_ota_ble_encode_open(uint8_t* buffer, size_t capacity,
				 const struct spotflow_ota_ble_header* header, const char* url,
				 const char* secret);
/**
 * @brief Encode READ
 * @param buffer Output message buffer.
 * @param capacity Output buffer capacity in bytes.
 * @param header Common operation and correlation fields.
 * @param offset Absolute artifact byte offset.
 * @param count Requested block length.
 * @return its length, -EINVAL for missing arguments, or -EMSGSIZE.
 */
int spotflow_ota_ble_encode_read(uint8_t* buffer, size_t capacity,
				 const struct spotflow_ota_ble_header* header, uint32_t offset,
				 uint16_t count);
/**
 * @brief Encode CLOSE
 * @param buffer Output message buffer.
 * @param capacity Output buffer capacity in bytes.
 * @param header Common operation and correlation fields.
 * @param consumed Number of artifact bytes consumed by the downloader.
 * @param reason Whether consumption completed or the transfer was aborted.
 * @return its length, -EINVAL for missing arguments, or -EMSGSIZE.
 */
int spotflow_ota_ble_encode_close(uint8_t* buffer, size_t capacity,
				  const struct spotflow_ota_ble_header* header, uint32_t consumed,
				  enum spotflow_ota_ble_close_reason reason);
/**
 * @brief Decode a response, borrowing its data
 * @param payload Encoded message bytes.
 * @param len Number of encoded bytes.
 * @param response Decoded response, borrowing data from payload.
 * @return 0, -EINVAL, or -EPROTO.
 */
int spotflow_ota_ble_decode_response(const uint8_t* payload, size_t len,
				     struct spotflow_ota_ble_response* response);
/**
 * @brief Convert a protocol status to 0 or a negative errno; unknown statuses use -EPROTO
 * @param status Wire response status.
 * @return 0 or a negative errno; unknown statuses use -EPROTO.
 */
int spotflow_ota_ble_status_error(uint8_t status);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPOTFLOW_OTA_BLE_PROTOCOL_H */
