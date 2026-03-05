#ifndef SPOTFLOW_H
#define SPOTFLOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPOTFLOW_LEVEL_ERROR = 0,
    SPOTFLOW_LEVEL_WARNING = 1,
    SPOTFLOW_LEVEL_INFO = 2,
    SPOTFLOW_LEVEL_DEBUG = 3
} SpotflowLevel;

bool Spotflow_init(void);
bool Spotflow_log(SpotflowLevel level, const char *tag, const char *fmt, ...);
void Spotflow_set_level(SpotflowLevel min_level);
uint32_t Spotflow_get_dropped(void);
uint32_t Spotflow_get_sent(void);

#define Spotflow_error(tag, fmt, ...)   \
    do { (void)Spotflow_log(SPOTFLOW_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__); } while (0)
#define Spotflow_warning(tag, fmt, ...) \
    do { (void)Spotflow_log(SPOTFLOW_LEVEL_WARNING, tag, fmt, ##__VA_ARGS__); } while (0)
#define Spotflow_info(tag, fmt, ...)    \
    do { (void)Spotflow_log(SPOTFLOW_LEVEL_INFO, tag, fmt, ##__VA_ARGS__); } while (0)
#define Spotflow_debug(tag, fmt, ...)   \
    do { (void)Spotflow_log(SPOTFLOW_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_H */
