#include <zcbor_encode.h>

#include <zephyr/logging/log.h>

#include "spotflow_build_id.h"
#include "net/spotflow_session_metadata.h"
#include "net/spotflow_mqtt.h"

#define MAX_KEY_COUNT 2

#define KEY_MESSAGE_TYPE 0x00
#define KEY_BUILD_ID 0x0E

#define SESSION_METADATA_MESSAGE_TYPE 1

/* Expecting at most 26 bytes, adding a safety margin */
#define MAX_CBOR_SIZE 32

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

static int cbor_encode_session_metadata(const uint8_t* build_id_data, size_t build_id_data_len,
					uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len);

int spotflow_session_metadata_send(void)
{
	const uint8_t* build_id = NULL;
	uint16_t build_id_len = 0;

	int rc;

#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	rc = spotflow_build_id_get(&build_id, &build_id_len);
	if (rc != 0) {
		LOG_DBG("Failed to get build ID for session metadata: %d", rc);
	}
#endif

	uint8_t buffer[MAX_CBOR_SIZE];
	size_t cbor_data_len = 0;

	rc = cbor_encode_session_metadata(build_id, build_id_len, buffer, sizeof(buffer),
					  &cbor_data_len);
	if (rc < 0) {
		LOG_DBG("Failed to encode session metadata: %d", rc);
		return rc;
	}

	return spotflow_mqtt_publish_cbor_msg(buffer, cbor_data_len);
}

static int cbor_encode_session_metadata(const uint8_t* build_id_data, size_t build_id_data_len,
					uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len)
{
	ZCBOR_STATE_E(state, 1, buffer, buffer_len, 1);

	bool succ;
	succ = zcbor_map_start_encode(state, MAX_KEY_COUNT);

	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SESSION_METADATA_MESSAGE_TYPE);

	if (build_id_data != NULL) {
		succ = succ && zcbor_uint32_put(state, KEY_BUILD_ID);
		succ = succ && zcbor_bstr_encode_ptr(state, build_id_data, build_id_data_len);
	}

	succ = succ && zcbor_map_end_encode(state, MAX_KEY_COUNT);
	if (succ != true) {
		LOG_DBG("Failed to encode session metadata: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*cbor_data_len = state->payload - buffer;

	return 0;
}
