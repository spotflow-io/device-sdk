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
    log_json_send(buffer);
#else
    log_cbor_send(buffer);
#endif
    // Optionally, call original log output to keep default behavior
    if (original_vprintf) {
        return original_vprintf(fmt, args);
    }

    return len;
}
