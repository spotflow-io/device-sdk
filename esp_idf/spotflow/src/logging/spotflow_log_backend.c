#include "logging/spotflow_log_backend.h"

static const char *TAG = "spotflow_testing";
#include <stdarg.h>

int spotflow_log_backend(const char *fmt, va_list args)
{
    int len = vsnprintf(NULL, 0, fmt, args);  // Get the required length
    
    // If the len is smaller than 0 or if the log is bigger than the set buffer size.
    if (len < 0 || len > CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
        SPOTFLOW_LOG("Spotflow Log buffer not enough. Increase size to incorporate long messages.");
        return 0;
    }

    // Dynamically assign memory in heap depending on the log size. 
    char *buffer = malloc(len + 1);  // +1 for null terminator
    if (!buffer) {
        SPOTFLOW_LOG("Error not able to assign memory.");
        return 0;
    }
    len++; // Increasing the size of len to include the null terminator
    len = vsnprintf(buffer, len, fmt, args);

#if CONFIG_USE_JSON_PAYLOAD
    char *log_severity = NULL;
    if (len > 0 && len < CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
        switch (buffer[0]) {
            case 'E': log_severity = "ERROR"; break;
            case 'W': log_severity = "WARNING"; break;
            case 'I': log_severity = "INFO"; break;
            case 'D': log_severity = "DEBUG"; break;
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
            case 'E': log_severity = 0x3C; break; //Error
            case 'W': log_severity = 0x32; break; //Warning
            case 'I': log_severity = 0x28;  break; //Info
            case 'D': log_severity = 0x1E; break; //Debug
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
