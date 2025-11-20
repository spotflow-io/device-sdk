
#include "spotflow.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_cbor.h"
#include "logging/spotflow_log_queue.h"
#include "net/spotflow_mqtt.h"
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

typedef enum {
	LOG_SEVERITY_ERROR = 0x3C, // Error
	LOG_SEVERITY_WARN = 0x32, // Warn
	LOG_SEVERITY_INFO = 0x28, // Info
	LOG_SEVERITY_DEBUG = 0x1E, // Debug
} LogSeverity;

/**
 * @brief Debugging Function not to be used in Production
 * 
 * @param buf 
 * @param len 
 */
// static void print_cbor_hex(const uint8_t* buf, size_t len)
// {
// 	SPOTFLOW_LOG("CBOR buffer (%zu bytes):\n", len);
// 	for (size_t i = 0; i < len; i++) {
// 		SPOTFLOW_LOG("%02X ", buf[i]); // print each byte as 2-digit hex
// 		if ((i + 1) % 16 == 0) // 16 bytes per line
// 			SPOTFLOW_LOG("\n");
// 	}
// 	SPOTFLOW_LOG("\n");
// }

/**
 * @brief To create the message format for logs in CBOR format
 * 
 * @param body 
 * @param severity 
 * @param out_len 
 * @return uint8_t* 
 */
uint8_t* spotflow_log_cbor(const char* log_template, char* body, const uint8_t severity, size_t* out_len,
		  const struct message_metadata* metadata)
{
	// Buffer to create array to cointain several items
	CborEncoder array_encoder;
	CborEncoder map_encoder;
	CborEncoder labels_encoder;

	uint8_t* buf = malloc(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN);
	if (!buf) {
		SPOTFLOW_LOG("Failed to allocate CBOR buffer");
		return NULL;
	}

	int body_len = strlen(body);

	// Check if the last character is a newline and remove it
	if (body_len > 0 && body[body_len - 1] == '\n') {
		body[body_len - 1] = '\0';
	}

	cbor_encoder_init(&array_encoder, buf, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, 0);
	cbor_encoder_create_map(&array_encoder, &map_encoder, CborIndefiniteLength); // {
	// Get device uptime in milliseconds since boot
	/* messageType: "LOG" */
	cbor_encode_uint(&map_encoder, KEY_MESSAGE_TYPE);
	cbor_encode_uint(&map_encoder, LOGS_MESSAGE_TYPE);

	cbor_encode_uint(&map_encoder, KEY_BODY);
	cbor_encode_text_stringz(&map_encoder, body);

	cbor_encode_uint(&map_encoder, KEY_SEVERITY);
	cbor_encode_uint(&map_encoder, severity);

	cbor_encode_uint(&map_encoder, KEY_BODY_TEMPLATE);
	cbor_encode_text_stringz(&map_encoder, log_template);
	//------------Metadata

	cbor_encode_uint(&map_encoder, KEY_SEQUENCE_NUMBER);
	cbor_encode_uint(&map_encoder, metadata->sequence_number);

	cbor_encode_uint(&map_encoder, KEY_DEVICE_UPTIME_MS);
	cbor_encode_uint(&map_encoder, metadata->uptime_ms);

	// labels → nested map with one element
	cbor_encode_uint(&map_encoder, KEY_LABELS);
	cbor_encoder_create_map(&map_encoder, &labels_encoder, 1); // {
	// // key: source (full string name inside labels map)
	cbor_encode_text_stringz(&labels_encoder, "source");
	if (metadata->source && metadata->source[0] != '\0') {
		cbor_encode_text_stringz(&labels_encoder, metadata->source);
	} else {
		SPOTFLOW_LOG("Source is missing or empty\n");
	}

	cbor_encoder_close_container(&map_encoder, &labels_encoder); // }

	cbor_encoder_close_container(&array_encoder, &map_encoder); // }
	// Allocate buffer for JSON string (adjust size as needed)

	*out_len = cbor_encoder_get_buffer_size(&array_encoder, buf);
	// print_cbor_hex(buf, *out_len);
	return buf;
}

/**
 * @brief Form and send the CBOR parameters
 * 
 * @param buffer 
 */
void spotflow_log_cbor_send(const char* fmt, char* buffer, const char log_severity,
		   const struct message_metadata* metadata)
{
	size_t len = strlen(buffer);
	uint8_t severity = 0;
	if (len > 0 && len < CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
		switch (log_severity) {
		case 'E':
			severity = LOG_SEVERITY_ERROR;
			break; //Error
		case 'W':
			severity = LOG_SEVERITY_WARN;
			break; //Warning
		case 'I':
			severity = LOG_SEVERITY_INFO;
			break; //Info
		case 'D':
			severity = LOG_SEVERITY_DEBUG;
			break; //Debug
		case 'V':
			severity = LOG_SEVERITY_DEBUG;
			break; //Verbose right now set to debug
		default:
			severity = 0x0;
			break; //In case no log type set it to 0, unknown level
		}

		uint8_t* clog_cbor =
		    spotflow_log_cbor(fmt, buffer, severity, &len, metadata); // It reuses the length

		spotflow_queue_push(clog_cbor, len);
		free(clog_cbor);
	}
}