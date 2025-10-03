#ifndef SPOTFLOW_LOG_FILTER_H
#define SPOTFLOW_LOG_FILTER_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/logging/log.h>

#define SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH 32

#ifdef __cplusplus
extern "C" {
#endif

bool spotflow_log_filter_allow_msg(struct log_msg* log_msg);

int spotflow_log_filter_update_from_c2d_message(uint8_t* payload, size_t len,
						uint64_t* configuration_version);

/* FIXME: optional configuration_version is passed as a pointer for simplicity,
 * clean up during refactoring
 */
int spotflow_log_filter_encode_report_cbor(uint8_t* buffer, size_t len,
					   uint64_t* configuration_version);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_LOG_FILTER_H */
