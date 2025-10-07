#include "logging/spotflow_log_backend.h"

static const char *TAG = "spotflow_testing";
#include <stdarg.h>

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
