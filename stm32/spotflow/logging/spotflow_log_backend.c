#include "spotflow_log_backend.h"
#include "spotflow_log_cbor.h"
#include <stdio.h>
#include <string.h>

/* =========================
   Internal Types
   ========================= */

typedef struct
{
    char buffer[SPOTFLOW_LOG_BUFFER_SIZE];
} spotflow_log_msg_t;

/* =========================
   Helpers
   ========================= */

static const char *spotflow_level_to_string(spotflow_log_level_t level)
{
    switch (level)
    {
        case SPOTFLOW_LOG_LEVEL_ERROR: return "E";
        case SPOTFLOW_LOG_LEVEL_WARN:  return "W";
        case SPOTFLOW_LOG_LEVEL_INFO:  return "I";
        case SPOTFLOW_LOG_LEVEL_DEBUG: return "D";
        default: return "?";
    }
}

static uint32_t spotflow_get_timestamp(void)
{
#if (SPOTFLOW_LOG_USE_TIMESTAMP == 1)
    uint32_t ticks = spotflow_port_get_tick_count();

#if (SPOTFLOW_LOG_TIMESTAMP_IN_MS == 1)
    return spotflow_port_tick_to_ms(ticks);
#else
    return ticks;
#endif

#else
    return 0;
#endif
}

/* =========================
   Logging Task
   ========================= */

static void spotflow_log_task(void *argument)
{
    
}

/* =========================
   Public API
   ========================= */

void spotflow_log_init(void)
{

}

void spotflow_log_write(spotflow_log_level_t level,
                        const char *tag,
                        const char *format, ...)
{
    static size_t sequence_counter = 0;

    char temp[SPOTFLOW_LOG_BUFFER_SIZE];

    va_list args;
    va_start(args, format);
    vsnprintf(temp, sizeof(temp), format, args);
    va_end(args);


    /* --- CBOR integration --- */
    struct message_metadata metadata = {
        .severity = spotflow_cbor_convert_log_level_to_severity(level),
        .uptime_ms = spotflow_get_timestamp(),
        .sequence_number = sequence_counter++,
        .source = tag
    };

    spotflow_log_cbor_send(format, temp, &metadata); // use level-char mapping
}
