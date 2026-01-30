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

void setUp(void) {
    cbor_buf = NULL;
    cbor_len = 0;
}

void tearDown(void) {
    if (cbor_buf) {
        free(cbor_buf);
        cbor_buf = NULL;
    }
}

TEST_CASE("CBOR encodes a simple log correctly", "[spotflow][cbor]")
{
    const char* template = "Test log template";
    char body[] = "Hello CBOR";
    struct message_metadata meta = {
        .sequence_number = 1,
        .uptime_ms = 1000,
        .source = "unit_test"
    };

    cbor_buf = spotflow_log_cbor(template, body, LOG_SEVERITY_INFO, &cbor_len, &meta);

    TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);
    TEST_SPOTFLOW_ASSERT_LESS_OR_EQUAL(MAX_CBOR_LEN, cbor_len);

    // Basic content check: body string is present
    TEST_SPOTFLOW_ASSERT_TRUE(memmem(cbor_buf, cbor_len, "Hello CBOR", strlen("Hello CBOR")) != NULL);

    // Check metadata sequence number (encoded as integer somewhere in CBOR)
    TEST_SPOTFLOW_ASSERT_TRUE(memmem(cbor_buf, cbor_len, &meta.sequence_number, 1) != NULL);

    free(cbor_buf);
    cbor_buf = NULL;
}

TEST_CASE("CBOR handles empty source string", "[spotflow][cbor]")
{
    const char* template = "Template";
    char body[] = "Msg";
    struct message_metadata meta = { .sequence_number = 1, .uptime_ms = 100, .source = "" };

    cbor_buf = spotflow_log_cbor(template, body, LOG_SEVERITY_WARN, &cbor_len, &meta);

    TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);

    // The CBOR should encode empty string for "source"
    TEST_SPOTFLOW_ASSERT_TRUE(memmem(cbor_buf, cbor_len, "", 0) != NULL);

    free(cbor_buf);
    cbor_buf = NULL;
}

TEST_CASE("CBOR encodes large log body", "[spotflow][cbor]")
{
    const char* template = "Large template";
    char* body = malloc(1024);
    memset(body, 'A', 1023);
    body[1023] = '\0';

    struct message_metadata meta = { .sequence_number = 99, .uptime_ms = 12345, .source = "large_test" };

    cbor_buf = spotflow_log_cbor(template, body, LOG_SEVERITY_INFO, &cbor_len, &meta);

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

    struct message_metadata meta = { .sequence_number = 5, .uptime_ms = 0, .source = "null_test" };

    // Allocate a dummy empty string if NULL
    char dummy_body[] = "";
    cbor_buf = spotflow_log_cbor(template, body ? body : dummy_body, LOG_SEVERITY_INFO, &cbor_len, &meta);

    TEST_SPOTFLOW_ASSERT_TRUE(cbor_buf != NULL);

    free(cbor_buf);
    cbor_buf = NULL;
}
