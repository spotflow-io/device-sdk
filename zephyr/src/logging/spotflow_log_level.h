#ifndef SPOTFLOW_LOG_LEVEL_H
#define SPOTFLOW_LOG_LEVEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert a Zephyr log level to protocol severity.
 * @param level Zephyr log level.
 * @return Protocol severity, or 0 for an unknown level.
 */
uint32_t spotflow_log_level_to_severity(uint8_t level);
/**
 * @brief Convert protocol severity to a Zephyr log level.
 * @param severity Protocol severity.
 * @return Zephyr log level, or debug for an unknown severity.
 */
uint8_t spotflow_log_level_from_severity(uint32_t severity);

#ifdef __cplusplus
}
#endif
#endif /* SPOTFLOW_LOG_LEVEL_H */
