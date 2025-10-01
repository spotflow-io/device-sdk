#include <stdio.h>
#include "spotflow.h"
#include "spotflow_log.h"

#ifndef CONFIG_USE_JSON_PAYLOAD
    #include "cbor.h"
#endif



#define CBOR_CHECK(a, str, goto_tag, ret_value, ...)                              \
    do                                                                            \
    {                                                                             \
        if ((a) != CborNoError)                                                   \
        {                                                                         \
            SPOTFLOW_LOG("%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

static void indent(int nestingLevel)
{
    while (nestingLevel--) {
        SPOTFLOW_LOG("  ");
    }
}

static void dumpbytes(const uint8_t *buf, size_t len)
{
    while (len--) {
        SPOTFLOW_LOG("%02X ", *buf++);
    }
}

/**
 * Decode CBOR data manually
 */
static CborError example_dump_cbor_buffer(CborValue *it, int nestingLevel)
{
    CborError ret = CborNoError;
    while (!cbor_value_at_end(it)) {
        CborType type = cbor_value_get_type(it);

        indent(nestingLevel);
        switch (type) {
        case CborArrayType: {
            CborValue recursed;
            assert(cbor_value_is_container(it));
            puts("Array[");
            ret = cbor_value_enter_container(it, &recursed);
            CBOR_CHECK(ret, "enter container failed", err, ret);
            ret = example_dump_cbor_buffer(&recursed, nestingLevel + 1);
            CBOR_CHECK(ret, "recursive dump failed", err, ret);
            ret = cbor_value_leave_container(it, &recursed);
            CBOR_CHECK(ret, "leave container failed", err, ret);
            indent(nestingLevel);
            puts("]");
            continue;
        }
        case CborMapType: {
            CborValue recursed;
            assert(cbor_value_is_container(it));
            puts("Map{");
            ret = cbor_value_enter_container(it, &recursed);
            CBOR_CHECK(ret, "enter container failed", err, ret);
            ret = example_dump_cbor_buffer(&recursed, nestingLevel + 1);
            CBOR_CHECK(ret, "recursive dump failed", err, ret);
            ret = cbor_value_leave_container(it, &recursed);
            CBOR_CHECK(ret, "leave container failed", err, ret);
            indent(nestingLevel);
            puts("}");
            continue;
        }
        case CborIntegerType: {
            int64_t val;
            ret = cbor_value_get_int64(it, &val);
            CBOR_CHECK(ret, "parse int64 failed", err, ret);
            SPOTFLOW_LOG("%lld\n", (long long)val);
            break;
        }
        case CborByteStringType: {
            uint8_t *buf;
            size_t n;
            ret = cbor_value_dup_byte_string(it, &buf, &n, it);
            CBOR_CHECK(ret, "parse byte string failed", err, ret);
            dumpbytes(buf, n);
            puts("");
            free(buf);
            continue;
        }
        case CborTextStringType: {
            char *buf;
            size_t n;
            ret = cbor_value_dup_text_string(it, &buf, &n, it);
            CBOR_CHECK(ret, "parse text string failed", err, ret);
            puts(buf);
            free(buf);
            continue;
        }
        case CborTagType: {
            CborTag tag;
            ret = cbor_value_get_tag(it, &tag);
            CBOR_CHECK(ret, "parse tag failed", err, ret);
            SPOTFLOW_LOG("Tag(%lld)\n", (long long)tag);
            break;
        }
        case CborSimpleType: {
            uint8_t type;
            ret = cbor_value_get_simple_type(it, &type);
            CBOR_CHECK(ret, "parse simple type failed", err, ret);
            SPOTFLOW_LOG("simple(%u)\n", type);
            break;
        }
        case CborNullType:
            puts("null");
            break;
        case CborUndefinedType:
            puts("undefined");
            break;
        case CborBooleanType: {
            bool val;
            ret = cbor_value_get_boolean(it, &val);
            CBOR_CHECK(ret, "parse boolean type failed", err, ret);
            puts(val ? "true" : "false");
            break;
        }
        case CborHalfFloatType: {
            uint16_t val;
            ret = cbor_value_get_half_float(it, &val);
            CBOR_CHECK(ret, "parse half float type failed", err, ret);
            SPOTFLOW_LOG("__f16(%04x)\n", val);
            break;
        }
        case CborFloatType: {
            float val;
            ret = cbor_value_get_float(it, &val);
            CBOR_CHECK(ret, "parse float type failed", err, ret);
            SPOTFLOW_LOG("%g\n", val);
            break;
        }
        case CborDoubleType: {
            double val;
            ret = cbor_value_get_double(it, &val);
            CBOR_CHECK(ret, "parse double float type failed", err, ret);
            SPOTFLOW_LOG("%g\n", val);
            break;
        }
        case CborInvalidType: {
            ret = CborErrorUnknownType;
            CBOR_CHECK(ret, "unknown cbor type", err, ret);
            break;
        }
        }

        ret = cbor_value_advance_fixed(it);
        CBOR_CHECK(ret, "fix value failed", err, ret);
    }
    return CborNoError;
err:
    return ret;
}


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

static vprintf_like_t original_vprintf = NULL;

static const char *TAG = "spotflow_testing";
#ifdef CONFIG_USE_JSON_PAYLOAD
char* log_json(char* body, const char* severity)
{
    char *separator = strchr(body, ':');
    if (separator != NULL) {
        // Start the body from the character after the colon
        body = separator + 2;
    }

    int body_len = strlen(body);

    // Check if the last character is a newline and remove it
    if (body_len > 0 && body[body_len - 1] == '\n') {
        body[body_len - 1] = '\0';
    }

    // Get device uptime in milliseconds since boot
    uint32_t uptime_ms = esp_log_timestamp();

    // Get device timestamp (UNIX epoch in ms)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t timestamp_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    // Allocate buffer for JSON string (adjust size as needed)
    char* json_str = malloc(612);
    if (!json_str) return NULL;

    // Format JSON string
    snprintf(json_str, 612,
        "{"
        "\"body\":\"%s\","
        // "\"body\": \"Test \n\",\n"
        "\"bodyTemplate\": \"\",\n"
        "\"bodyTemplateValues\": [\"\"],\n"
        "\"severity\":\"%s\","
        "\"deviceUptimeMs\":%lu,"
        "\"deviceTimestampMs\":%llu"
        "}",
        body,
         severity, uptime_ms, timestamp_ms
    );
    return json_str;
}

#else
uint8_t* log_cbor(char* body, uint8_t severity, size_t *out_len)
{
    // Buffer to create array to cointain several items
    CborEncoder array_encoder;
    CborEncoder map_encoder;
    uint8_t *buf = malloc(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN);
    if (!buf) {
        SPOTFLOW_LOG("Failed to allocate CBOR buffer");
        return NULL;
    }
    
    char *separator = strchr(body, ':');
    if (separator != NULL) {
        // Start the body from the character after the colon
        body = separator + 2;
    }

    int body_len = strlen(body);

    // Check if the last character is a newline and remove it
    if (body_len > 0 && body[body_len - 1] == '\n') {
        body[body_len - 1] = '\0';
    }

    
    uint32_t uptime_ms = esp_log_timestamp();

    // Get device timestamp (UNIX epoch in ms)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t timestamp_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    cbor_encoder_init(&array_encoder, buf, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, 0);
    cbor_encoder_create_map(&array_encoder, &map_encoder, 4); // {
    // Get device uptime in milliseconds since boot
    /* messageType: "LOG" */
    cbor_encode_uint(&map_encoder, KEY_MESSAGE_TYPE);
    cbor_encode_uint(&map_encoder, LOGS_MESSAGE_TYPE);
    
    cbor_encode_uint(&map_encoder, KEY_BODY);
    cbor_encode_text_stringz(&map_encoder, body);

    cbor_encode_uint(&map_encoder, KEY_SEVERITY);
    cbor_encode_uint(&map_encoder, severity);

    cbor_encode_uint(&map_encoder, KEY_BODY_TEMPLATE);
    cbor_encode_text_stringz(&map_encoder, "");

    cbor_encoder_close_container(&array_encoder, &map_encoder); // }
    // Allocate buffer for JSON string (adjust size as needed)
    
    *out_len = cbor_encoder_get_buffer_size(&array_encoder, buf);
    SPOTFLOW_LOG("encoded buffer size %d", *out_len);
    #ifdef CONFIG_SPOTFLOW_DEBUG_MESSAGE_TERMINAL
        CborParser root_parser;
        CborValue it;
        cbor_parser_init(buf, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, 0, &root_parser, &it);
        example_dump_cbor_buffer(&it, 0);
    #endif
    return buf;
}

#endif


int spotflow_log_backend(const char *fmt, va_list args)
{
    // Format the log string into a buffer
    char buffer[CONFIG_SPOTFLOW_LOG_BUFFER_SIZE];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);

#if CONFIG_USE_JSON_PAYLOAD
    char *log_severity = NULL;
    if (len > 0 && len < CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
        switch (buffer[0]) {
            case 'I': log_severity = "INFO"; break;
            case 'D': log_severity = "DEBUG"; break;
            case 'W': log_severity = "WARNING"; break;
            case 'E': log_severity = "ERROR"; break;
            case 'V': log_severity = "VERBOSE"; break;
            default: log_severity = "NONE"; break;
        }

        if(mqtt_connected)
        {
            const char *clog_json = log_json(buffer, log_severity);
            esp_mqtt_client_publish(client, "ingest-json", clog_json , 0, 1, 0);
            // SPOTFLOW_LOG( "%s\n", clog_json);
            free(clog_json);
        }
    }
#else
    uint8_t log_severity = 0;
    if (len > 0 && len < CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
        switch (buffer[0]) {
            case 'I': log_severity = 0x28;  break; //Info
            case 'D': log_severity = 0x1E; break; //Debug
            case 'W': log_severity = 0x32; break; //Warning
            case 'E': log_severity = 0x3C; break; //Error
            case 'V': log_severity = 0x28; break; //Verbose right now set to info
            default: log_severity = 0x0; break; //In case no log type set it to 0, unknown level
        }

        if(mqtt_connected)
        {
            size_t len;
            uint8_t *clog_cbor = log_cbor(buffer, log_severity, &len);
            esp_mqtt_client_publish(client, "ingest-cbor", (const char*)clog_cbor , len, 1, 0);
            free(clog_cbor);
        }
    }

#endif
    // Optionally, call original log output to keep default behavior
    if (original_vprintf) {
        return original_vprintf(fmt, args);
    }

    return len;
}

// Initilize the spotlow log backend
void spotflow_init(void)
{
    original_vprintf = esp_log_set_vprintf(spotflow_log_backend);
}
