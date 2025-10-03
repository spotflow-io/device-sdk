#ifndef SPOTFLOW_LOG_FILTER_H
#define SPOTFLOW_LOG_FILTER_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/logging/log.h>

#ifdef __cplusplus
extern "C" {
#endif

bool spotflow_log_filter_allow_msg(struct log_msg* log_msg);

int spotflow_log_filter_update_from_c2d_message(uint8_t* payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_LOG_FILTER_H */
