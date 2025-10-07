#include "logging/spotflow_log_json.h"

/**
 * @brief To use the json messaging format to send logs to server
 * 
 * @return char* 
 */
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