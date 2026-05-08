#include "test_common.h"

/* optimized property keys */
#define KEY_MESSAGE_TYPE 0x00
#define LOGS_MESSAGE_TYPE 0x00
#define KEY_BODY 0x01
#define KEY_BODY_TEMPLATE 0x02
/* (unused for now) */
#define KEY_BODY_TEMPLATE_VALUES 0x03
#define KEY_SEVERITY 0x04
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_SEQUENCE_NUMBER 0x0D

typedef enum {
	LOG_SEVERITY_ERROR = 0x3C, // Error
	LOG_SEVERITY_WARN = 0x32, // Warn
	LOG_SEVERITY_INFO = 0x28, // Info
	LOG_SEVERITY_DEBUG = 0x1E, // Debug
} LogSeverity;

// Maximum CBOR buffer length used in production
#define MAX_CBOR_LEN CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN

static uint8_t* cbor_buf = NULL;
static size_t cbor_len = 0;

void setUp(void)
{
	cbor_buf = NULL;
	cbor_len = 0;
}

void tearDown(void)
{
	if (cbor_buf) {
		free(cbor_buf);
		cbor_buf = NULL;
	}
}

static bool cbor_text_equals(CborValue* element, const char* expected_text, size_t expected_len)
{
	if (!cbor_value_is_text_string(element)) {
		return false;
	}

	size_t value_len;
	if (cbor_value_calculate_string_length(element, &value_len) != CborNoError ||
	    value_len != expected_len) {
		return false;
	}

	if (value_len == 0U) {
		return true;
	}

	char* buf = (char*)malloc(value_len + 1U);
	if (buf == NULL) {
		return false;
	}

	size_t copied = value_len + 1U;
	bool match = false;

	if ((cbor_value_copy_text_string(element, buf, &copied, NULL) == CborNoError) &&
	    (strncmp(buf, expected_text, value_len) == 0)) {
		match = true;
	}

	free(buf);
	return match;
}

static bool get_cbor_key(CborValue* element, uint64_t* key)
{
	if (!cbor_value_is_unsigned_integer(element) ||
	    cbor_value_get_uint64(element, key) != CborNoError) {
		return false;
	}

	return (cbor_value_advance(element) == CborNoError);
}

static bool find_cbor_text_value(CborValue* element, uint32_t key, const char* expected_text,
				 size_t expected_len)
{
	while (!cbor_value_at_end(element)) {
		uint64_t current_key;

		if (!get_cbor_key(element, &current_key)) {
			(void)cbor_value_advance(element);
			continue;
		}

		if (current_key == (uint64_t)key) {
			return cbor_text_equals(element, expected_text, expected_len);
		}

		if (cbor_value_advance(element) != CborNoError) {
			break;
		}
	}

	return false;
}

/* Helper function to verify CBOR integer key with text string value */
static bool contains_cbor_text_value(const uint8_t* cbor_data, size_t cbor_len, uint32_t key,
				     const char* expected_text)
{
	CborParser parser;
	CborValue map, element;
	size_t expected_len;

	if ((cbor_data == NULL) || (expected_text == NULL) || (cbor_len == 0U)) {
		return false;
	}

	expected_len = strlen(expected_text);

	if ((cbor_parser_init(cbor_data, cbor_len, 0U, &parser, &map) != CborNoError) ||
	    (!cbor_value_is_map(&map)) ||
	    (cbor_value_enter_container(&map, &element) != CborNoError)) {
		return false;
	}
	bool result = find_cbor_text_value(&element, key, expected_text, expected_len);
	return result;
}

TEST_CASE("CBOR encodes a simple log correctly", "[spotflow][cbor]")
{
	const char* template_str = "Test log template";
	char body[] = "Hello CBOR";
	struct message_metadata meta = { .sequence_number = 1U,
					 .uptime_ms = 1000U,
					 .severity = LOG_SEVERITY_INFO,
					 .source = "unit_test" };

	cbor_buf = spotflow_log_cbor(template_str, body, &cbor_len, &meta);

	TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);
	TEST_SPOTFLOW_ASSERT_LESS_OR_EQUAL(MAX_CBOR_LEN, cbor_len);

	/* Verify message type */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_MESSAGE_TYPE, LOGS_MESSAGE_TYPE));

	/* Verify body content */
	TEST_SPOTFLOW_ASSERT_TRUE(contains_cbor_text_value(cbor_buf, cbor_len, KEY_BODY, body));

	/* Verify body template */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_text_value(cbor_buf, cbor_len, KEY_BODY_TEMPLATE, template_str));

	/* Verify severity */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_SEVERITY, LOG_SEVERITY_INFO));

	/* Verify metadata - sequence number */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_SEQUENCE_NUMBER, 1U));

	/* Verify metadata - uptime */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_DEVICE_UPTIME_MS, 1000U));

	free(cbor_buf);
	cbor_buf = NULL;
}

TEST_CASE("CBOR handles empty source string", "[spotflow][cbor]")
{
	const char* template_str = "Template";
	char body[] = "Msg";
	struct message_metadata meta = { .sequence_number = 1U,
					 .uptime_ms = 100U,
					 .source = "",
					 .severity = LOG_SEVERITY_WARN };

	cbor_buf = spotflow_log_cbor(template_str, body, &cbor_len, &meta);

	TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);
	TEST_SPOTFLOW_ASSERT_TRUE(cbor_len > 0U);

	/* Verify basic structure - message type should be LOGS_MESSAGE_TYPE */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_MESSAGE_TYPE, LOGS_MESSAGE_TYPE));

	/* Verify metadata fields */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_SEQUENCE_NUMBER, 1U));

	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_DEVICE_UPTIME_MS, 100U));

	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_uint_value(cbor_buf, cbor_len, KEY_SEVERITY, LOG_SEVERITY_WARN));

	/* Verify body template */
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_text_value(cbor_buf, cbor_len, KEY_BODY_TEMPLATE, template_str));

	/* Verify body message */
	TEST_SPOTFLOW_ASSERT_TRUE(contains_cbor_text_value(cbor_buf, cbor_len, KEY_BODY, body));

	free(cbor_buf);
	cbor_buf = NULL;
}

TEST_CASE("CBOR encodes large log body", "[spotflow][cbor]")
{
	const char* template = "Large template";
	char* body = malloc(1024);
	memset(body, 'A', 1023);
	body[1023] = '\0';

	struct message_metadata meta = { .sequence_number = 99,
					 .uptime_ms = 12345,
					 .severity = LOG_SEVERITY_INFO,
					 .source = "large_test" };

	cbor_buf = spotflow_log_cbor(template, body, &cbor_len, &meta);

	TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);

	TEST_SPOTFLOW_ASSERT_TRUE(cbor_len > 0);
	free(body);
	free(cbor_buf);
	cbor_buf = NULL;
}

TEST_CASE("CBOR handles NULL body safely", "[spotflow][cbor]")
{
	const char* template = "Template";
	char* body = NULL;

	struct message_metadata meta = { .sequence_number = 5,
					 .uptime_ms = 0,
					 .severity = LOG_SEVERITY_INFO,
					 .source = "null_test" };

	// Allocate a dummy empty string if NULL
	char dummy_body[] = "";
	cbor_buf = spotflow_log_cbor(template, body ? body : dummy_body, &cbor_len, &meta);

	TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);

	free(cbor_buf);
	cbor_buf = NULL;
}
