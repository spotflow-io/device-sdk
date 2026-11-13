#include "ota/protocol/spotflow_ota_ble_protocol.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

static void encode_header(uint8_t* buffer, const struct spotflow_ota_ble_header* header,
			  enum spotflow_ota_ble_operation operation);

int spotflow_ota_ble_decode_header(const uint8_t* payload, size_t len,
				   struct spotflow_ota_ble_header* header)
{
	if (payload == NULL || header == NULL || len < SPOTFLOW_OTA_BLE_HEADER_SIZE) {
		return -EINVAL;
	}

	*header = (struct spotflow_ota_ble_header){
		.operation = payload[0],
		.transfer_id = sys_get_le32(payload + 1),
		.request_id = sys_get_le32(payload + 5),
	};
	return 0;
}

int spotflow_ota_ble_encode_open(uint8_t* buffer, size_t capacity,
				 const struct spotflow_ota_ble_header* header, const char* url,
				 const char* secret)
{
	if (buffer == NULL || header == NULL || url == NULL || secret == NULL) {
		return -EINVAL;
	}

	size_t url_len = strlen(url);
	size_t secret_len = strlen(secret);
	size_t message_len = SPOTFLOW_OTA_BLE_OPEN_HEADER_SIZE + url_len + secret_len;

	if (url_len > SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH ||
	    secret_len > SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH || message_len > capacity) {
		return -EMSGSIZE;
	}

	encode_header(buffer, header, SPOTFLOW_OTA_BLE_OPEN);
	sys_put_le16(url_len, buffer + SPOTFLOW_OTA_BLE_HEADER_SIZE);
	buffer[11] = (uint8_t)secret_len;
	memcpy(buffer + SPOTFLOW_OTA_BLE_OPEN_HEADER_SIZE, url, url_len);
	memcpy(buffer + SPOTFLOW_OTA_BLE_OPEN_HEADER_SIZE + url_len, secret, secret_len);
	return (int)message_len;
}

int spotflow_ota_ble_encode_read(uint8_t* buffer, size_t capacity,
				 const struct spotflow_ota_ble_header* header, uint32_t offset,
				 uint16_t count)
{
	if (buffer == NULL || header == NULL) {
		return -EINVAL;
	}
	if (capacity < SPOTFLOW_OTA_BLE_READ_SIZE) {
		return -EMSGSIZE;
	}

	encode_header(buffer, header, SPOTFLOW_OTA_BLE_READ);
	sys_put_le32(offset, buffer + SPOTFLOW_OTA_BLE_HEADER_SIZE);
	sys_put_le16(count, buffer + SPOTFLOW_OTA_BLE_HEADER_SIZE + sizeof(offset));
	return SPOTFLOW_OTA_BLE_READ_SIZE;
}

int spotflow_ota_ble_encode_close(uint8_t* buffer, size_t capacity,
				  const struct spotflow_ota_ble_header* header, uint32_t consumed,
				  enum spotflow_ota_ble_close_reason reason)
{
	if (buffer == NULL || header == NULL) {
		return -EINVAL;
	}
	if (capacity < SPOTFLOW_OTA_BLE_CLOSE_SIZE) {
		return -EMSGSIZE;
	}

	encode_header(buffer, header, SPOTFLOW_OTA_BLE_CLOSE);
	sys_put_le32(consumed, buffer + SPOTFLOW_OTA_BLE_HEADER_SIZE);
	buffer[SPOTFLOW_OTA_BLE_HEADER_SIZE + sizeof(consumed)] = reason;
	return SPOTFLOW_OTA_BLE_CLOSE_SIZE;
}

int spotflow_ota_ble_decode_response(const uint8_t* payload, size_t len,
				     struct spotflow_ota_ble_response* response)
{
	if (payload == NULL || response == NULL) {
		return -EINVAL;
	}
	if (len < SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE) {
		return -EPROTO;
	}

	*response = (struct spotflow_ota_ble_response){ 0 };
	(void)spotflow_ota_ble_decode_header(payload, len, &response->header);
	response->status = payload[SPOTFLOW_OTA_BLE_HEADER_SIZE];

	uint8_t operation = response->header.operation ^ SPOTFLOW_OTA_BLE_RESPONSE_FLAG;
	if (operation < SPOTFLOW_OTA_BLE_OPEN || operation > SPOTFLOW_OTA_BLE_CLOSE) {
		return -EPROTO;
	}
	if (response->status != SPOTFLOW_OTA_BLE_STATUS_OK || operation == SPOTFLOW_OTA_BLE_CLOSE) {
		return len == SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE ? 0 : -EPROTO;
	}
	if (len < SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE ||
	    (operation == SPOTFLOW_OTA_BLE_OPEN && len != SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE)) {
		return -EPROTO;
	}

	response->position = sys_get_le32(payload + SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE);
	response->data = payload + SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE;
	response->data_len = len - SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE;
	return 0;
}

int spotflow_ota_ble_status_error(uint8_t status)
{
	switch (status) {
	case SPOTFLOW_OTA_BLE_STATUS_OK:
		return 0;
	case SPOTFLOW_OTA_BLE_STATUS_ACCESS_DENIED:
		return -EACCES;
	case SPOTFLOW_OTA_BLE_STATUS_TRANSIENT_FAILURE:
		return -EAGAIN;
	case SPOTFLOW_OTA_BLE_STATUS_ARTIFACT_TOO_LARGE:
		return -EFBIG;
	case SPOTFLOW_OTA_BLE_STATUS_INVALID_REQUEST:
	default:
		return -EPROTO;
	}
}

static void encode_header(uint8_t* buffer, const struct spotflow_ota_ble_header* header,
			  enum spotflow_ota_ble_operation operation)
{
	buffer[0] = operation;
	sys_put_le32(header->transfer_id, buffer + 1);
	sys_put_le32(header->request_id, buffer + 5);
}
