#ifndef SPOTFLOW_BACKEND_LOG_H
#define SPOTFLOW_BACKEND_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdint.h>
#include "spotflow.h"

struct message_metadata {
    uint8_t severity;
    unsigned long uptime_ms;
    size_t sequence_number;
    const char* source;
};

void spotflow_log_init(void);

void spotflow_log_write(spotflow_log_level_t level,
                        const char *tag,
                        const char *format, ...);


/* =========================
   Logging Macros
   ========================= */

#if (SPOTFLOW_LOG_LEVEL >= SPOTFLOW_LOG_LEVEL_ERROR)
#define SPOTFLOW_LOGE(tag, fmt, ...) \
    spotflow_log_write(SPOTFLOW_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#define SPOTFLOW_LOGE(tag, fmt, ...)
#endif

#if (SPOTFLOW_LOG_LEVEL >= SPOTFLOW_LOG_LEVEL_WARN)
#define SPOTFLOW_LOGW(tag, fmt, ...) \
    spotflow_log_write(SPOTFLOW_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#else
#define SPOTFLOW_LOGW(tag, fmt, ...)
#endif

#if (SPOTFLOW_LOG_LEVEL >= SPOTFLOW_LOG_LEVEL_INFO)
#define SPOTFLOW_LOGI(tag, fmt, ...) \
    spotflow_log_write(SPOTFLOW_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#else
#define SPOTFLOW_LOGI(tag, fmt, ...)
#endif

#if (SPOTFLOW_LOG_LEVEL >= SPOTFLOW_LOG_LEVEL_DEBUG)
#define SPOTFLOW_LOGD(tag, fmt, ...) \
    spotflow_log_write(SPOTFLOW_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define SPOTFLOW_LOGD(tag, fmt, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif // SPOTFLOW_BACKEND_LOG_H
