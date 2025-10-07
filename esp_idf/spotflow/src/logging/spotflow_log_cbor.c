
#include "logging/spotflow_log_cbor.h"
#include "cbor.h"

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

/**
 * @brief To create the message format for logs in CBOR format
 * 
 * @param body 
 * @param severity 
 * @param out_len 
 * @return uint8_t* 
 */
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

    return buf;
}