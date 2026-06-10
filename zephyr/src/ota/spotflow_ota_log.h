#ifndef SPOTFLOW_OTA_LOG_H
#define SPOTFLOW_OTA_LOG_H

#include <stdbool.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota_persistence.h"
#include "ota/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* spotflow_ota_log_result_name(enum spotflow_ota_result result);

const char* spotflow_ota_log_attempt_error_name(enum spotflow_ota_attempt_error error);

const char* spotflow_ota_log_phase_name(enum spotflow_ota_phase phase);

void spotflow_ota_log_loaded_attempt(const struct spotflow_ota_persisted_attempt* attempt,
				     bool has_attempt);

void spotflow_ota_log_loaded_probation(const struct spotflow_ota_probation* probation,
				       bool has_probation);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_LOG_H */
