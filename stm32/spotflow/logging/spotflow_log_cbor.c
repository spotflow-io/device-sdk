#include "spotflow_log_cbor.h"
#include "tinycbor/src/cbor.h"
#include <string.h>
#include <stdlib.h>
#include "spotflow.h" // For SPOTFLOW_LOG, queue, mqtt

/* CBOR property keys */
#define KEY_MESSAGE_TYPE 0x00
#define LOGS_MESSAGE_TYPE 0x00
#define KEY_BODY 0x01
#define KEY_BODY_TEMPLATE 0x02
#define KEY_BODY_TEMPLATE_VALUES 0x03
#define KEY_SEVERITY 0x04
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_SEQUENCE_NUMBER 0x0D

typedef enum {
    LOG_SEVERITY_ERROR = 0x3C,
    LOG_SEVERITY_WARN  = 0x32,
    LOG_SEVERITY_INFO  = 0x28,
    LOG_SEVERITY_DEBUG = 0x1E,
} LogSeverity;

/* Convert character log level to internal severity */
static uint8_t spotflow_log_cbor_convert_char_log_lvl(const char lvl)
{
    switch(lvl) {
        case 'E': return LOG_SEVERITY_ERROR;
        case 'W': return LOG_SEVERITY_WARN;
        case 'I': return LOG_SEVERITY_INFO;
        case 'D': return LOG_SEVERITY_DEBUG;
        case 'V': return LOG_SEVERITY_DEBUG;
        default: return 0;
    }
}

uint8_t* spotflow_log_cbor(const char* log_template,
                           char* body,
                           size_t* out_len,
                           const struct message_metadata* metadata)
{
    CborEncoder array_encoder, map_encoder, labels_encoder;
    uint8_t* buf = malloc(SPOTFLOW_CBOR_LOG_MAX_LEN);
    if(!buf) {
//        SPOTFLOW_LOG("Failed to allocate CBOR buffer");
        return NULL;
    }

    size_t body_len = strlen(body);
    if(body_len > 0 && body[body_len - 1] == '\n')
        body[body_len - 1] = '\0';

    cbor_encoder_init(&array_encoder, buf, SPOTFLOW_CBOR_LOG_MAX_LEN, 0);
    cbor_encoder_create_map(&array_encoder, &map_encoder, 7);

    /* messageType */
    cbor_encode_uint(&map_encoder, KEY_MESSAGE_TYPE);
    cbor_encode_uint(&map_encoder, LOGS_MESSAGE_TYPE);

    /* body */
    cbor_encode_uint(&map_encoder, KEY_BODY);
    cbor_encode_text_stringz(&map_encoder, body);

    /* severity */
    cbor_encode_uint(&map_encoder, KEY_SEVERITY);
    cbor_encode_uint(&map_encoder, metadata->severity);

    /* body template */
    cbor_encode_uint(&map_encoder, KEY_BODY_TEMPLATE);
    cbor_encode_text_stringz(&map_encoder, log_template);

    /* sequence number */
    cbor_encode_uint(&map_encoder, KEY_SEQUENCE_NUMBER);
    cbor_encode_uint(&map_encoder, metadata->sequence_number);

    /* device uptime */
    cbor_encode_uint(&map_encoder, KEY_DEVICE_UPTIME_MS);
    cbor_encode_uint(&map_encoder, metadata->uptime_ms);

    /* labels map */
    cbor_encode_uint(&map_encoder, KEY_LABELS);
    cbor_encoder_create_map(&map_encoder, &labels_encoder, 1);
    cbor_encode_text_stringz(&labels_encoder, "source");
    cbor_encode_text_stringz(&labels_encoder, metadata->source ? metadata->source : "");
    cbor_encoder_close_container(&map_encoder, &labels_encoder);

    cbor_encoder_close_container(&array_encoder, &map_encoder);

    *out_len = cbor_encoder_get_buffer_size(&array_encoder, buf);
    return buf;
}

void spotflow_log_cbor_send(const char* fmt,
                            char* buffer,
                            const struct message_metadata* metadata)
{
    size_t len = strlen(buffer);
    uint8_t severity = metadata->severity;

        if(len > 0 && len < SPOTFLOW_LOG_BUFFER_SIZE) {
            uint8_t* clog_cbor = spotflow_log_cbor(fmt, buffer, &len, metadata);
            if(clog_cbor) {
                spotflow_queue_push(clog_cbor, len);
                // spotflow_mqtt_notify_action(SPOTFLOW_MQTT_NOTIFY_LOGS);
                free(clog_cbor);
            }
        }
}

/**
 * @brief Convert log level to Cloud severity values
 *
 * @param lvl
 * @return uint32_t
 */
uint32_t spotflow_cbor_convert_log_level_to_severity(uint8_t lvl)
{
	switch (lvl) {
	case 'E':
		return LOG_SEVERITY_ERROR;
	case 'W':
		return LOG_SEVERITY_WARN;
	case 'I':
		return LOG_SEVERITY_INFO;
	case 'D':
		return LOG_SEVERITY_DEBUG;
	default:
		return 0; /* unknown level */
	}
}
