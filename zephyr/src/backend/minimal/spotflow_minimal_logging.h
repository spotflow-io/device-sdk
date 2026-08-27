#ifndef SPOTFLOW_MINIMAL_LOGGING_H
#define SPOTFLOW_MINIMAL_LOGGING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Update the runtime filter for all log sources when enabled
 *
 * @param level Zephyr log level to apply
 */
void spotflow_log_backend_try_set_runtime_filter(uint32_t level);

/**
 * @brief Encode and send the oldest queued log
 *
 * @param workspace Buffer used to encode the log as CBOR
 * @param workspace_size Size of @p workspace in bytes
 *
 * @return 1 when a log was sent, 0 when the queue is empty, or a negative errno
 */
int spotflow_minimal_log_process(uint8_t* workspace, size_t workspace_size);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_MINIMAL_LOGGING_H */
