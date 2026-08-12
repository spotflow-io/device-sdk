#include <string.h>

#include <zcbor_decode.h>

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/ztest.h>

#include <spotflow/session_metadata.h>

#include "net/spotflow_session_metadata.h"
#include "net/spotflow_transport.h"

LOG_MODULE_REGISTER(spotflow_net);

#define KEY_MESSAGE_TYPE 0x00
#define KEY_LABELS 0x05
#define KEY_DEVICE_RUN_ID 0x1E
#define SESSION_METADATA_MESSAGE_TYPE 1

static struct spotflow_session_metadata_labels session_metadata_labels;
static uint32_t fake_random_seed;

static bool search_uint32_key(zcbor_state_t* state, uint32_t key);
static bool search_tstr_key(zcbor_state_t* state, const char* key);
static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected);
static bool decode_expected_tstr_key(zcbor_state_t* state, void* expected);
static bool session_metadata_has_key(const uint8_t* payload, size_t len, uint32_t key);

struct spotflow_session_metadata_labels spotflow_override_session_metadata_labels(void)
{
	return session_metadata_labels;
}

int spotflow_transport_send_ingest_cbor(uint8_t* payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	return 0;
}

void z_impl_sys_rand_get(void* dst, size_t len)
{
	uint8_t* bytes = dst;

	for (size_t i = 0; i < len; i++) {
		bytes[i] = ++fake_random_seed;
	}
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	session_metadata_labels = (struct spotflow_session_metadata_labels){ 0 };
	fake_random_seed = 0;
}

ZTEST(session_metadata, test_encodes_typed_labels)
{
	static const struct spotflow_session_label labels[] = {
		{
			.key = "channel",
			.type = SPOTFLOW_SESSION_LABEL_STRING,
			.value.string = "staging",
		},
		{
			.key = "line",
			.type = SPOTFLOW_SESSION_LABEL_INT,
			.value.integer = -4,
		},
		{
			.key = "ratio",
			.type = SPOTFLOW_SESSION_LABEL_FLOAT,
			.value.floating = 1.5,
		},
		{
			.key = "enabled",
			.type = SPOTFLOW_SESSION_LABEL_BOOL,
			.value.boolean = true,
		},
	};
	uint8_t buffer[CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE];
	size_t cbor_data_len;
	uint32_t message_type;
	uint64_t device_run_id;
	int64_t line;
	double ratio;
	bool enabled;

	session_metadata_labels = (struct spotflow_session_metadata_labels){
		.items = labels,
		.count = ARRAY_SIZE(labels),
	};

	zassert_ok(spotflow_session_metadata_encode(buffer, sizeof(buffer), &cbor_data_len));

	ZCBOR_STATE_D(state, 3, buffer, cbor_data_len, 1, 0);
	bool success = zcbor_unordered_map_start_decode(state);

	success = success && search_uint32_key(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_decode(state, &message_type);
	success = success && search_uint32_key(state, KEY_DEVICE_RUN_ID);
	success = success && zcbor_uint64_decode(state, &device_run_id);
	success = success && search_uint32_key(state, KEY_LABELS);
	success = success && zcbor_unordered_map_start_decode(state);
	success = success && search_tstr_key(state, "channel");
	success = success &&
		zcbor_tstr_expect(state, &(struct zcbor_string){ (const uint8_t*)"staging", 7 });
	success = success && search_tstr_key(state, "line");
	success = success && zcbor_int64_decode(state, &line);
	success = success && search_tstr_key(state, "ratio");
	success = success && zcbor_float64_decode(state, &ratio);
	success = success && search_tstr_key(state, "enabled");
	success = success && zcbor_bool_decode(state, &enabled);
	success = success && zcbor_unordered_map_end_decode(state);
	success = success && zcbor_unordered_map_end_decode(state);

	zassert_true(success, "Failed to decode session metadata: %d", zcbor_peek_error(state));
	zassert_equal(message_type, SESSION_METADATA_MESSAGE_TYPE);
	zassert_not_equal(device_run_id, 0);
	zassert_equal(line, -4);
	zassert_equal(ratio, 1.5);
	zassert_true(enabled);
}

ZTEST(session_metadata, test_omits_invalid_labels)
{
	static const struct spotflow_session_label labels[] = {
		{
			.key = "",
			.type = SPOTFLOW_SESSION_LABEL_STRING,
			.value.string = "invalid",
		},
	};
	uint8_t buffer[CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE];
	size_t cbor_data_len;

	session_metadata_labels = (struct spotflow_session_metadata_labels){
		.items = labels,
		.count = ARRAY_SIZE(labels),
	};

	zassert_ok(spotflow_session_metadata_encode(buffer, sizeof(buffer), &cbor_data_len));
	zassert_false(session_metadata_has_key(buffer, cbor_data_len, KEY_LABELS));
}

ZTEST(session_metadata, test_omits_oversized_labels)
{
	static const struct spotflow_session_label labels[] = {
		{
			.key = "long-label",
			.type = SPOTFLOW_SESSION_LABEL_STRING,
			.value.string =
				"this label value intentionally exceeds the small encoding buffer",
		},
	};
	uint8_t buffer[64];
	size_t cbor_data_len;

	session_metadata_labels = (struct spotflow_session_metadata_labels){
		.items = labels,
		.count = ARRAY_SIZE(labels),
	};

	zassert_ok(spotflow_session_metadata_encode(buffer, sizeof(buffer), &cbor_data_len));
	zassert_false(session_metadata_has_key(buffer, cbor_data_len, KEY_LABELS));
}

ZTEST_SUITE(session_metadata, NULL, NULL, before_each, NULL, NULL);

static bool search_uint32_key(zcbor_state_t* state, uint32_t key)
{
	return zcbor_unordered_map_search(decode_expected_uint32_key, state, &key);
}

static bool search_tstr_key(zcbor_state_t* state, const char* key)
{
	struct zcbor_string expected = {
		.value = (const uint8_t*)key,
		.len = strlen(key),
	};

	return zcbor_unordered_map_search(decode_expected_tstr_key, state, &expected);
}

static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected)
{
	return zcbor_uint32_pexpect(state, expected);
}

static bool decode_expected_tstr_key(zcbor_state_t* state, void* expected)
{
	return zcbor_tstr_expect(state, expected);
}

static bool session_metadata_has_key(const uint8_t* payload, size_t len, uint32_t key)
{
	ZCBOR_STATE_D(state, 2, payload, len, 1, 0);

	return zcbor_unordered_map_start_decode(state) && search_uint32_key(state, key);
}
