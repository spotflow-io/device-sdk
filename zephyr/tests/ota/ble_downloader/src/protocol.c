#include "ota/protocol/spotflow_ota_ble_protocol.h"

#include <errno.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(ble_artifact_protocol, NULL, NULL, NULL, NULL, NULL);

ZTEST(ble_artifact_protocol, test_request_wire_vectors)
{
	const struct spotflow_ota_ble_header header = {
		.transfer_id = 0x04030201,
		.request_id = 0x08070605,
	};
	uint8_t buffer[SPOTFLOW_OTA_BLE_OPEN_MAX_SIZE];
	/* Keep literal wire vectors independent of the encoders being tested. */
	const uint8_t expected_open[] = {
		1, /* OPEN operation. */
		1,   2,	  3, 4, /* Transfer ID 0x04030201: uint32 LE. */
		5,   6,	  7, 8, /* Request ID 0x08070605: uint32 LE. */
		1,   0, /* URL length: uint16 LE. */
		1, /* Secret length: uint8. */
		'u', 's', /* URL followed by secret, without terminators. */
	};
	const uint8_t expected_read[] = {
		2, /* READ operation. */
		1,  2,	3,  4, /* Transfer ID: uint32 LE. */
		5,  6,	7,  8, /* Request ID: uint32 LE. */
		9,  10, 11, 12, /* Offset 0x0c0b0a09: uint32 LE. */
		13, 14, /* Requested count 0x0e0d: uint16 LE. */
	};
	const uint8_t expected_close[] = {
		3, /* CLOSE operation. */
		1, 2,  3,  4, /* Transfer ID: uint32 LE. */
		5, 6,  7,  8, /* Request ID: uint32 LE. */
		9, 10, 11, 12, /* Consumed offset 0x0c0b0a09: uint32 LE. */
		1, /* ABORTED close reason. */
	};

	zassert_equal(spotflow_ota_ble_encode_open(buffer, sizeof(buffer), &header, "u", "s"),
		      sizeof(expected_open));
	zassert_mem_equal(buffer, expected_open, sizeof(expected_open));
	zassert_equal(
		spotflow_ota_ble_encode_read(buffer, sizeof(buffer), &header, 0x0c0b0a09, 0x0e0d),
		sizeof(expected_read));
	zassert_mem_equal(buffer, expected_read, sizeof(expected_read));
	zassert_equal(spotflow_ota_ble_encode_close(buffer, sizeof(buffer), &header, 0x0c0b0a09,
						    SPOTFLOW_OTA_BLE_CLOSE_ABORTED),
		      sizeof(expected_close));
	zassert_mem_equal(buffer, expected_close, sizeof(expected_close));
	zassert_equal(spotflow_ota_ble_encode_read(buffer, 1, &header, 0, 1), -EMSGSIZE);
	zassert_equal(spotflow_ota_ble_encode_open(NULL, 0, &header, "u", "s"), -EINVAL);
}

ZTEST(ble_artifact_protocol, test_response_wire_vectors_and_lengths)
{
	uint8_t payload[] = {
		0x82, /* READ operation with the response bit set. */
		1,    2,  3,  4, /* Transfer ID 0x04030201: uint32 LE. */
		5,    6,  7,  8, /* Request ID 0x08070605: uint32 LE. */
		0, /* OK status. */
		9,    10, 11, 12, /* Block offset 0x0c0b0a09: uint32 LE. */
		0xab, /* One byte of artifact data. */
	};
	struct spotflow_ota_ble_response response;

	zassert_ok(spotflow_ota_ble_decode_response(payload, sizeof(payload), &response));
	zassert_equal(response.header.transfer_id, 0x04030201);
	zassert_equal(response.header.request_id, 0x08070605);
	zassert_equal(response.position, 0x0c0b0a09);
	zassert_equal(response.data_len, 1);
	zassert_equal(response.data[0], 0xab);
	/* Truncate the uint32 position by one byte. */
	zassert_equal(spotflow_ota_ble_decode_response(
			      payload, SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE - 1, &response),
		      -EPROTO);
	payload[0] = 0x81; /* OPEN response: size is allowed, artifact data is not. */
	zassert_equal(spotflow_ota_ble_decode_response(payload, sizeof(payload), &response),
		      -EPROTO);
	zassert_ok(spotflow_ota_ble_decode_response(payload, SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE,
						    &response));
	payload[0] = 0x83; /* CLOSE response: only correlation fields and status. */
	zassert_ok(spotflow_ota_ble_decode_response(payload, SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE,
						    &response));
	zassert_equal(spotflow_ota_ble_decode_response(payload, SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE,
						       &response),
		      -EPROTO);
	payload[0] = 2; /* READ request operation: missing the required response bit. */
	zassert_equal(spotflow_ota_ble_decode_response(
			      payload, SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE, &response),
		      -EPROTO);
}

ZTEST(ble_artifact_protocol, test_status_mapping)
{
	zassert_ok(spotflow_ota_ble_status_error(SPOTFLOW_OTA_BLE_STATUS_OK));
	zassert_equal(spotflow_ota_ble_status_error(SPOTFLOW_OTA_BLE_STATUS_INVALID_REQUEST),
		      -EPROTO);
	zassert_equal(spotflow_ota_ble_status_error(SPOTFLOW_OTA_BLE_STATUS_ACCESS_DENIED),
		      -EACCES);
	zassert_equal(spotflow_ota_ble_status_error(SPOTFLOW_OTA_BLE_STATUS_TRANSIENT_FAILURE),
		      -EAGAIN);
	zassert_equal(spotflow_ota_ble_status_error(SPOTFLOW_OTA_BLE_STATUS_ARTIFACT_TOO_LARGE),
		      -EFBIG);
	zassert_equal(spotflow_ota_ble_status_error(0xff), -EPROTO);
}
