#include "net/spotflow_session_metadata.h"

#include "spotflow_build_id.h"
#include "net/spotflow_transport.h"
#ifdef CONFIG_SPOTFLOW_OTA
#include "ota/spotflow_ota.h"
#endif /* CONFIG_SPOTFLOW_OTA */

#include <zcbor_encode.h>

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#define KEY_MESSAGE_TYPE 0x00
#define KEY_BUILD_ID 0x0E
#define KEY_DEVICE_RUN_ID 0x1E
#define KEY_LAST_UPDATE_ATTEMPT_ID 41

#define SESSION_METADATA_MESSAGE_TYPE 1

/* Expecting at most 34 bytes, adding a safety margin */
#define MAX_CBOR_SIZE 64

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static uint64_t device_run_id = 0;

static int cbor_encode_session_metadata(const uint8_t* build_id_data, size_t build_id_data_len,
					uint64_t run_id, bool include_last_update_attempt_id,
					uint64_t last_update_attempt_id, uint8_t* buffer,
					size_t buffer_len, size_t* cbor_data_len);

int spotflow_session_metadata_send(void)
{
	uint8_t buffer[MAX_CBOR_SIZE];
	size_t cbor_data_len = 0;
	int rc = spotflow_session_metadata_encode(buffer, sizeof(buffer), &cbor_data_len);

	if (rc < 0) {
		return rc;
	}

	return spotflow_transport_send_ingest_cbor(buffer, cbor_data_len);
}

int spotflow_session_metadata_encode(uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len)
{
	const uint8_t* build_id = NULL;
	uint16_t build_id_len = 0;
	bool include_last_update_attempt_id = false;
	uint64_t last_update_attempt_id = 0;

	/* Generate device run ID once per boot */
	if (device_run_id == 0) {
		uint32_t rand_high = sys_rand32_get();
		uint32_t rand_low = sys_rand32_get();
		device_run_id = ((uint64_t)rand_high << 32) | rand_low;
		LOG_INF("Generated device run ID: %" PRIu64, device_run_id);
	}

#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	int rc = spotflow_build_id_get(&build_id, &build_id_len);
	if (rc != 0) {
		LOG_DBG("Failed to get build ID for session metadata: %d", rc);
	}
#endif /* CONFIG_SPOTFLOW_GENERATE_BUILD_ID */

#ifdef CONFIG_SPOTFLOW_OTA
	include_last_update_attempt_id = true;
	last_update_attempt_id = spotflow_ota_get_last_received_attempt_id();
#endif /* CONFIG_SPOTFLOW_OTA */

	return cbor_encode_session_metadata(build_id, build_id_len, device_run_id,
					    include_last_update_attempt_id, last_update_attempt_id,
					    buffer, buffer_len, cbor_data_len);
}

static int cbor_encode_session_metadata(const uint8_t* build_id_data, size_t build_id_data_len,
					uint64_t run_id, bool include_last_update_attempt_id,
					uint64_t last_update_attempt_id, uint8_t* buffer,
					size_t buffer_len, size_t* cbor_data_len)
{
	ZCBOR_STATE_E(state, 1, buffer, buffer_len, 1);
	size_t key_count = 2;

	if (build_id_data != NULL) {
		key_count++;
	}
	if (include_last_update_attempt_id) {
		key_count++;
	}

	bool succ;
	succ = zcbor_map_start_encode(state, key_count);

	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SESSION_METADATA_MESSAGE_TYPE);

	succ = succ && zcbor_uint32_put(state, KEY_DEVICE_RUN_ID);
	succ = succ && zcbor_uint64_put(state, run_id);

	if (build_id_data != NULL) {
		succ = succ && zcbor_uint32_put(state, KEY_BUILD_ID);
		succ = succ && zcbor_bstr_encode_ptr(state, build_id_data, build_id_data_len);
	}

	if (include_last_update_attempt_id) {
		succ = succ && zcbor_uint32_put(state, KEY_LAST_UPDATE_ATTEMPT_ID);
		succ = succ && zcbor_uint64_put(state, last_update_attempt_id);
	}

	succ = succ && zcbor_map_end_encode(state, key_count);
	if (succ != true) {
		LOG_DBG("Failed to encode session metadata: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*cbor_data_len = state->payload - buffer;

	return 0;
}
