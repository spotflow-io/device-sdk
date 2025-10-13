#include "logging/spotflow_log_json.h"
#include "logging/spotflow_log_queue.h"

/**
 * @brief To use the json messaging format to send logs to server
 * 
 * @return char* 
 */
char* log_json(const char *fem, char* body, const char* severity, size_t *out_len, const struct message_metadata *metadata)
{
    *out_len = strlen(body);

    // Check if the last character is a newline and remove it
    if (*out_len > 0 && body[*out_len - 1] == '\n') {
        body[*out_len - 1] = '\0';
    }

    // Get device uptime in milliseconds since boot
    uint32_t uptime_ms = esp_log_timestamp();

    // Get device timestamp (UNIX epoch in ms)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t timestamp_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    // Allocate buffer for JSON string (adjust size as needed)
    char* json_str = malloc(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN);
    if (!json_str) return NULL;

    // Format JSON string
    snprintf(json_str, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN,
        "{"
        "\"body\":\"%s\","
        // "\"body\": \"Test \n\",\n"
        "\"bodyTemplate\": \"\",\n"
        "\"bodyTemplateValues\": [\"\"],\n"
        "\"severity\":\"%s\","
        // "\"deviceUptimeMs\":%lu,"
        // "\"deviceTimestampMs\":%llu"
        "}",
        body,
        severity
//      metadata->uptime_ms, timestamp_ms
    );
    return json_str;
}

/**
 * @brief Form and send the JSON parameters
 * 
 * @param fmt 
 * @param buffer 
 * @param log_severity 
 * @param metadata 
 */
void log_json_send(const char *fmt, char* buffer, const char log_severity, const struct message_metadata *metadata)
{
    char *severity = NULL;
    int len = strlen(buffer);
    if (len > 0 && len < CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
        switch (buffer[0]) {
            case 'E': severity = "ERROR"; break;
            case 'W': severity = "WARNING"; break;
            case 'I': severity = "INFO"; break;
            case 'D': severity = "DEBUG"; break;
            case 'V': severity = "DEBUG"; break;
            // case 'V': log_severity = "VERBOSE"; break;
            default: severity = "NONE"; break;
        }

        size_t len;
        const char *clog_json = log_json(fmt, buffer, severity, &len, metadata);
        queue_push((const char*) clog_json, len);

        if(atomic_load(&mqtt_connected))
        {
            char *queue_buffer = malloc(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN);
            
            while (queue_read(queue_buffer) != -1 && atomic_load(&mqtt_connected)) //Check if mqtt disconnect event is not generated.
            {
                esp_mqtt_client_publish(client, "ingest-json", (const char*)queue_buffer , CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, 1, 0); // Treat it as a NULL terminated string
            }

            free(queue_buffer);
        }
        free(clog_json);
    }
}