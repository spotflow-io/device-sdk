#include "net/spotflow_session_metadata.h"

#include "spotflow_build_id.h"
#include "net/spotflow_transport.h"
#include <spotflow/session_metadata.h>
#ifdef CONFIG_SPOTFLOW_OTA
#include "ota/spotflow_ota.h"
#endif /* CONFIG_SPOTFLOW_OTA */

#include <zcbor_encode.h>

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#define KEY_MESSAGE_TYPE 0x00
#define KEY_LABELS 0x05
#define KEY_BUILD_ID 0x0E
#define KEY_DEVICE_RUN_ID 0x1E
#define KEY_LAST_UPDATE_ATTEMPT_ID 41

#define SESSION_METADATA_MESSAGE_TYPE 1

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static uint64_t device_run_id = 0;

static int cbor_encode_session_metadata(const uint8_t* build_id_data, size_t build_id_data_len,
					uint64_t run_id, bool include_last_update_attempt_id,
					uint64_t last_update_attempt_id,
					const struct spotflow_session_metadata_labels* labels,
					uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len);
static bool cbor_encode_labels(zcbor_state_t* state,
			       const struct spotflow_session_metadata_labels* labels);

struct spotflow_session_metadata_labels __weak spotflow_override_session_metadata_labels(void)
{
	return (struct spotflow_session_metadata_labels){ 0 };
}

int spotflow_session_metadata_send(void)
{
	static uint8_t buffer[CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE];
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
	int rc;
	struct spotflow_session_metadata_labels labels =
		spotflow_override_session_metadata_labels();
	const struct spotflow_session_metadata_labels no_labels = { 0 };

	if (labels.items == NULL && labels.count != 0) {
		LOG_WRN("Session metadata labels have no item array; omitting labels");
		labels = no_labels;
	}

	for (size_t i = 0; i < labels.count; i++) {
		if (labels.items[i].key == NULL || labels.items[i].key[0] == '\0' ||
		    (labels.items[i].type == SPOTFLOW_SESSION_LABEL_STRING &&
		     labels.items[i].value.string == NULL) ||
		    labels.items[i].type < SPOTFLOW_SESSION_LABEL_STRING ||
		    labels.items[i].type > SPOTFLOW_SESSION_LABEL_BOOL) {
			LOG_WRN("Session metadata label %zu is invalid; omitting labels", i);
			labels = no_labels;
			break;
		}
	}

	/* Generate device run ID once per boot */
	if (device_run_id == 0) {
		uint32_t rand_high = sys_rand32_get();
		uint32_t rand_low = sys_rand32_get();
		device_run_id = ((uint64_t)rand_high << 32) | rand_low;
		LOG_INF("Generated device run ID: %" PRIu64, device_run_id);
	}

#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	rc = spotflow_build_id_get(&build_id, &build_id_len);
	if (rc != 0) {
		LOG_DBG("Failed to get build ID for session metadata: %d", rc);
	}
#endif /* CONFIG_SPOTFLOW_GENERATE_BUILD_ID */

#ifdef CONFIG_SPOTFLOW_OTA
	include_last_update_attempt_id = true;
	last_update_attempt_id = spotflow_ota_get_last_received_attempt_id();
#endif /* CONFIG_SPOTFLOW_OTA */

	rc = cbor_encode_session_metadata(build_id, build_id_len, device_run_id,
					  include_last_update_attempt_id, last_update_attempt_id,
					  &labels, buffer, buffer_len, cbor_data_len);
	if (rc == -EMSGSIZE && labels.count > 0) {
		LOG_WRN("Session metadata labels exceed the configured buffer; omitting labels");
		rc = cbor_encode_session_metadata(
			build_id, build_id_len, device_run_id, include_last_update_attempt_id,
			last_update_attempt_id, &no_labels, buffer, buffer_len, cbor_data_len);
	}

	return rc;
}

static int cbor_encode_session_metadata(const uint8_t* build_id_data, size_t build_id_data_len,
					uint64_t run_id, bool include_last_update_attempt_id,
					uint64_t last_update_attempt_id,
					const struct spotflow_session_metadata_labels* labels,
					uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len)
{
	ZCBOR_STATE_E(state, 1, buffer, buffer_len, 1);
	size_t key_count = 2;

	if (build_id_data != NULL) {
		key_count++;
	}
	if (include_last_update_attempt_id) {
		key_count++;
	}
	if (labels->count > 0) {
		key_count++;
	}

	bool succ;
	succ = zcbor_map_start_encode(state, key_count);

	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SESSION_METADATA_MESSAGE_TYPE);

	succ = succ && zcbor_uint32_put(state, KEY_DEVICE_RUN_ID);
	succ = succ && zcbor_uint64_put(state, run_id);

	if (labels->count > 0) {
		succ = succ && cbor_encode_labels(state, labels);
	}

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
		if (zcbor_peek_error(state) == ZCBOR_ERR_NO_PAYLOAD) {
			return -EMSGSIZE;
		}

		return -EINVAL;
	}

	*cbor_data_len = state->payload - buffer;

	return 0;
}

static bool cbor_encode_labels(zcbor_state_t* state,
			       const struct spotflow_session_metadata_labels* labels)
{
	bool success = zcbor_uint32_put(state, KEY_LABELS);

	success = success && zcbor_map_start_encode(state, labels->count);
	for (size_t i = 0; i < labels->count && success; i++) {
		const struct spotflow_session_label* label = &labels->items[i];

		success = success && zcbor_tstr_put_term(state, label->key, SIZE_MAX);
		switch (label->type) {
		case SPOTFLOW_SESSION_LABEL_STRING:
			success = success &&
				zcbor_tstr_put_term(state, label->value.string, SIZE_MAX);
			break;
		case SPOTFLOW_SESSION_LABEL_INT:
			success = success && zcbor_int64_put(state, label->value.integer);
			break;
		case SPOTFLOW_SESSION_LABEL_FLOAT:
			success = success && zcbor_float64_put(state, label->value.floating);
			break;
		case SPOTFLOW_SESSION_LABEL_BOOL:
			success = success && zcbor_bool_put(state, label->value.boolean);
			break;
		default:
			return false;
		}
	}

	LOG_DBG("Encoded %zu session metadata labels", labels->count);

	return success && zcbor_map_end_encode(state, labels->count);
}
