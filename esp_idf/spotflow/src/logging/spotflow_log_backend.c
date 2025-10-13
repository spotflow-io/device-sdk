#include "logging/spotflow_log_backend.h"

#ifndef CONFIG_USE_JSON_PAYLOAD
    #include "logging/spotflow_log_cbor.h"
#else
    #include "logging/spotflow_log_json.h"
#endif
static const char *TAG = "spotflow_testing";
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int spotflow_log_backend(const char *fmt, va_list args)
{   
    int log_seq_arg = 0;
    static size_t sequence = 0;
    struct message_metadata metadata = {0};
    sequence++;
    metadata.sequence_number = sequence;

    // Copy fmt so we can modify it safely 
    char *fmt_copy = strdup(fmt);
    if (!fmt_copy) {
        SPOTFLOW_LOG("Memory allocation failed for fmt copy.");
        return 0;
    }

    // Create a copy of va_list args
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, fmt_copy, args_copy);  // Get the required length

    char log_severity = fmt_copy[0];
    for(const char *p = fmt_copy; *p || *p == ':'; p++)
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
                    metadata.uptime_ms = va_arg(args_copy, int);
                    log_seq_arg++;
                }
                break;
            }
            case 'i': {
                if(log_seq_arg == 1)
                {
                    metadata.uptime_ms = va_arg(args_copy, int);
                }
                break;
            }
            case 'u': {
                if(log_seq_arg == 0)
                {
                    log_seq_arg++; //Log issue
                }
                if (is_long && log_seq_arg == 1) {
                    metadata.uptime_ms = va_arg(args_copy, unsigned long);
                    log_seq_arg++;
                } else if (log_seq_arg == 1){
                    unsigned int val = va_arg(args_copy, unsigned int);
                    log_seq_arg++;
                }
                break;
            }
            case 's': {
                if(log_seq_arg == 2)
                {
                    metadata.source = va_arg(args_copy, const char *);
                    log_seq_arg++;
                }
                break;
            }
            case 'c': {
                    if(log_seq_arg == 0)
                    {
                        metadata.severity =  va_arg(args_copy, int);
                        log_seq_arg++;
                    }
                break;
            }
            default:
                break;
        }

    }

    char *fmt_copy_1 = fmt_copy; // Just a location for the original memory to free later.
    char *separator = strchr(fmt_copy, ':');
    if (separator != NULL) {
        // Start the body from the character after the colon
        fmt_copy = separator + 2;
    }

    // If the len is smaller than 0 or if the log is bigger than the set buffer size.
    if (len < 0 || len > CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
        SPOTFLOW_LOG("Spotflow Log buffer not enough. Increase size to incorporate long messages.");
        va_end(args_copy);
        free(fmt_copy_1);
        return 0;
    }

    // Dynamically assign memory in heap depending on the log size. 
    char *buffer = malloc(len + 1);  // +1 for null terminator
    if (!buffer) {
        SPOTFLOW_LOG("Error not able to assign memory.");
        return 0;
    }

    len = vsnprintf(buffer, len+1, fmt_copy, args_copy);
#if CONFIG_USE_JSON_PAYLOAD
    log_json_send(buffer, metadata.severity);
#else
    log_cbor_send(fmt_copy, buffer, log_severity, &metadata);
#endif

    free(buffer);
    va_end(args_copy);
    free(fmt_copy_1);
    // Optionally, call original log output to keep default behavior
    if (original_vprintf) {
        return original_vprintf(fmt, args);
    }
    return len;
}
