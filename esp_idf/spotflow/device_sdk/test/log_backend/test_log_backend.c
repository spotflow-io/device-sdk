#include "test_common.h"
#include "cbor.h"

static bool contains_cbor_key(uint8_t* cbor_data, size_t cbor_len, uint32_t key,
			      uint32_t expected_value);

// Global variables for queue
queue_msg_t queue_msg; // Structure to hold the message from the queue

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

// Helper function to read the queue and verify the message content
static void read_queue_and_verify(const char* expected_msg)
{
	bool message_read = spotflow_queue_read(&queue_msg);
	TEST_SPOTFLOW_ASSERT_TRUE(message_read);

	// Convert the queue buffer to a string and verify it contains the expected message
	char* message = (char*)queue_msg.ptr;
	TEST_SPOTFLOW_ASSERT_TRUE(memmem((uint8_t*)message, queue_msg.len, (uint8_t*)expected_msg,
					 strlen(expected_msg)) != NULL);

	// Clean up
	spotflow_queue_free(&queue_msg);
}

static void setUP_init()
{ // Setup
	spotflow_queue_init();
	spotflow_mqtt_app_start();
	esp_log_set_vprintf(spotflow_log_backend);
}
// Test Case 1: Testing Info-level log (ESP_LOGI)
TEST_CASE("Test spotflow_log_backend with ESP_LOGI", "[spotflow][log_backend]")
{
	setUP_init();
	const char* log_msg = "Test log info";
	ESP_LOGI("TAG", "%s", log_msg);

	// Read the message from the queue and verify
	read_queue_and_verify(log_msg);

	// Cleanup
	esp_log_set_vprintf(NULL); // Reset to default vprintf behavior
}

// Test Case 2: Testing Error-level log (ESP_LOGE) and metadata encoding
TEST_CASE("Test spotflow_log_backend with ESP_LOGE", "[spotflow][log_backend]")
{
	setUP_init();

	const char* log_msg = "Test error log";
	ESP_LOGE("TAG", "%s", log_msg);

	// Read the message from the queue and verify
	read_queue_and_verify(log_msg);

	// Cleanup
	esp_log_set_vprintf(NULL); // Reset to default vprintf behavior
}

// Test Case 3: Verifying CBOR keys and values for different severity levels
TEST_CASE("Test CBOR keys for different severities", "[spotflow][log_backend][cbor]")
{
	setUP_init();

	const char* log_msg = "Test severity log";

	// Test for ERROR severity
	ESP_LOGE("TAG", "%s", log_msg);
	read_queue_and_verify(log_msg); // First verify the message is logged

	// Now check if the CBOR data has the correct key-value pairs
	uint8_t* cbor_data = (uint8_t*)queue_msg.ptr; // The CBOR encoded message
	size_t cbor_len = queue_msg.len;

	// Verifying that the CBOR contains the correct keys for ERROR severity (0x3C)
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_key(cbor_data, cbor_len, KEY_MESSAGE_TYPE, LOGS_MESSAGE_TYPE));
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_key(cbor_data, cbor_len, KEY_SEVERITY, LOG_SEVERITY_ERROR));

	// Test for INFO severity
	ESP_LOGI("TAG", "%s", log_msg);
	read_queue_and_verify(log_msg); // Verify the message

	// Verify INFO severity (0x28) is present in the CBOR
	cbor_data = (uint8_t*)queue_msg.ptr;
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_key(cbor_data, cbor_len, KEY_SEVERITY, LOG_SEVERITY_INFO));

	// Test for DEBUG severity
	ESP_LOGD("TAG", "%s", log_msg);
	read_queue_and_verify(log_msg);

	// Verify DEBUG severity (0x1E)
	cbor_data = (uint8_t*)queue_msg.ptr;
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_key(cbor_data, cbor_len, KEY_SEVERITY, LOG_SEVERITY_DEBUG));

	// Test for WARN severity
	ESP_LOGW("TAG", "%s", log_msg);
	read_queue_and_verify(log_msg);

	// Verify WARN severity (0x32)
	cbor_data = (uint8_t*)queue_msg.ptr;
	TEST_SPOTFLOW_ASSERT_TRUE(
	    contains_cbor_key(cbor_data, cbor_len, KEY_SEVERITY, LOG_SEVERITY_WARN));

	// Cleanup
	esp_log_set_vprintf(NULL); // Reset to default vprintf behavior
}

// Test Case 4: Testing large message that exceeds buffer size
TEST_CASE("Test spotflow_log_backend with large message", "[spotflow][log_backend][large_message]")
{
	// Setup
	setUP_init();

	const char* long_msg = "A very long message to test how the system handles messages larger "
			       "than the buffer size...";
	size_t max_msg_size =
	    CONFIG_SPOTFLOW_LOG_BUFFER_SIZE + 100; // Test message size larger than the buffer

	// Create a large message
	char* large_msg = malloc(max_msg_size);
	memset(large_msg, 'A', max_msg_size - 1);
	large_msg[max_msg_size - 1] = '\0'; // Null-terminate the string

	ESP_LOGI("TAG", "%s", large_msg); // Log the large message

	// Now verify the message in the queue
	read_queue_and_verify(large_msg);

	free(large_msg); // Free the allocated memory

	// Cleanup
	esp_log_set_vprintf(NULL); // Reset to default vprintf behavior
}

// Helper function to check if CBOR data contains a specific key-value pair
static bool contains_cbor_key(const uint8_t* cbor_data, size_t cbor_len, uint32_t key,
			      uint32_t expected_value)
{
	bool result = false;
	CborParser parser;
	CborValue map;
	CborValue element;
	CborError err;

	/* MISRA C: Check for NULL pointer */
	if (cbor_data == NULL) {
		return false;
	}

	/* MISRA C: Check for zero length */
	if (cbor_len == 0U) {
		return false;
	}

	/* Initialize CBOR parser */
	err = cbor_parser_init(cbor_data, cbor_len, 0U, &parser, &map);
	if (err != CborNoError) {
		return false;
	}

	/* Verify that the top-level element is a map */
	if (!cbor_value_is_map(&map)) {
		return false;
	}

	/* Enter the map */
	err = cbor_value_enter_container(&map, &element);
	if (err != CborNoError) {
		return false;
	}

	/* Iterate through map entries */
	while (!cbor_value_at_end(&element)) {
		uint64_t current_key = 0U;
		uint64_t current_value = 0U;

		/* Read the key (must be an unsigned integer) */
		if (cbor_value_is_unsigned_integer(&element)) {
			err = cbor_value_get_uint64(&element, &current_key);
			if (err != CborNoError) {
				break;
			}

			/* Advance to the value */
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}

			/* Check if we found the target key */
			if (current_key == (uint64_t)key) {
				/* Read the value (must be an unsigned integer) */
				if (cbor_value_is_unsigned_integer(&element)) {
					err = cbor_value_get_uint64(&element, &current_value);
					if (err != CborNoError) {
						break;
					}

					/* Compare with expected value */
					if (current_value == (uint64_t)expected_value) {
						result = true;
					}
				}
				/* Key found, exit loop regardless of value match */
				break;
			}

			/* Skip the value for non-matching keys */
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}
		} else {
			/* Invalid key type - skip this key-value pair */
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}
		}
	}

	/* Leave the container (not strictly necessary for verification) */
	(void)cbor_value_leave_container(&map, &element);

	return result;
}
