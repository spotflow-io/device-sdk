#ifndef SPOTFLOW_OTA_PERSISTENCE_H
#define SPOTFLOW_OTA_PERSISTENCE_H

#include <stdbool.h>
#include <stddef.h>

#include "ota/spotflow_ota_records_cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_ota_persistence_init(void);

int spotflow_ota_persistence_load_attempt(struct spotflow_ota_persisted_attempt* attempt,
					  bool* has_attempt);

int spotflow_ota_persistence_save_attempt(const struct spotflow_ota_persisted_attempt* attempt);

int spotflow_ota_persistence_load_probation(struct spotflow_ota_probation* probation,
					    bool* has_probation);

int spotflow_ota_persistence_save_probation(const struct spotflow_ota_probation* probation);

int spotflow_ota_persistence_clear_probation(void);

int spotflow_ota_persistence_load_installed_version(const char* slug, char* version,
						    size_t version_len, bool* has_version);

int spotflow_ota_persistence_save_installed_version(const char* slug, const char* version);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PERSISTENCE_H */
