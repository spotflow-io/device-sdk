#include "logging/spotflow_log_backend.h"

#ifndef CONFIG_USE_JSON_PAYLOAD
    #include "logging/spotflow_log_cbor.h"
#else
    #include "logging/spotflow_log_json.h"
#endif
static const char *TAG = "spotflow_testing";
#include <stdarg.h>


int spotflow_log_backend(const char *fmt, va_list args)
{   
    int log_seq_arg = 0;
    static size_t sequence = 0;
    struct message_metadata metadata = {0};
    sequence++;

    int len = vsnprintf(NULL, 0, fmt, args);  // Get the required length

    char log_severity = fmt[0];
    for(const char *p = fmt; *p || *p == ':'; p++)
    {
        if (*p != '%') continue;  // skip until '%'
        p++; // move past '%'
        if (*p == '%') continue; // skip literal "%%"

        // Handle length modifiers like 'l'
        bool is_long = false;
        if (*p == 'l') {
            is_long = true;
            p++;  // move to actual specifier, e.g. 'u', 'd'
        }

        switch (*p) {
            case 'd': {
                if(log_seq_arg == 1)
                {
                    metadata.uptime_ms = va_arg(args, int);
                    log_seq_arg++;
                }
                break;
            }
            case 'i': {
                if(log_seq_arg == 1)
                {
                    metadata.uptime_ms = va_arg(args, int);
                }
                break;
            }
            case 'u': {
                if(log_seq_arg == 0)
                {
                    log_seq_arg++; //Log issue
                }
                if (is_long && log_seq_arg == 1) {
                    metadata.uptime_ms = va_arg(args, unsigned long);
                    log_seq_arg++;
                } else if (log_seq_arg == 1){
                    unsigned int val = va_arg(args, unsigned int);
                    log_seq_arg++;
                }
                break;
            }
            case 's': {
                if(log_seq_arg == 2)
                {
                    metadata.source = va_arg(args, const char *);
                    log_seq_arg++;
                }
                break;
            }
            case 'c': {
                    if(log_seq_arg == 0)
                    {
                        metadata.severity =  va_arg(args, int);
                        log_seq_arg++;
                    }
                break;
            }
            default:
                break;
        }

    }

    char *separator = strchr(fmt, ':');
    if (separator != NULL) {
        // Start the body from the character after the colon
        fmt = separator + 2;
    }

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

    len = vsnprintf(buffer, len+1, fmt, args);
#if CONFIG_USE_JSON_PAYLOAD
    log_json_send(buffer, metadata.severity);
#else
    log_cbor_send(fmt, buffer, log_severity, &metadata);
#endif

    free(buffer);
    // Optionally, call original log output to keep default behavior
    if (original_vprintf) {
        return original_vprintf(fmt, args);
    }
    return len;
}
