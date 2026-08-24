#ifndef SPOTFLOW_OTA_RESULTS_H
#define SPOTFLOW_OTA_RESULTS_H

#include <stdbool.h>
#include <stdint.h>

#include "ota/persistence/spotflow_ota_records.h"

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_ota_results_persist_attempt(const struct spotflow_ota_persisted_attempt* attempt);

int spotflow_ota_results_load_attempt(uint64_t attempt_id,
				      struct spotflow_ota_persisted_attempt* attempt,
				      bool* has_attempt);

int spotflow_ota_results_prepare_attempt(const struct spotflow_ota_persisted_attempt* attempt);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_RESULTS_H */
