#include "ota/core/spotflow_ota_results.h"

#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"

#include <errno.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

int spotflow_ota_results_persist_attempt(const struct spotflow_ota_persisted_attempt* attempt)
{
	if (attempt == NULL) {
		return -EINVAL;
	}

	int rc = spotflow_ota_persistence_save_attempt(attempt);
	if (rc < 0) {
		LOG_ERR("Failed to persist OTA attempt %llu results: %d",
			(unsigned long long)attempt->attempt_id, rc);
	}

	return rc;
}

int spotflow_ota_results_load_attempt(uint64_t attempt_id,
				      struct spotflow_ota_persisted_attempt* attempt,
				      bool* has_attempt)
{
	if (attempt_id == 0 || attempt == NULL || has_attempt == NULL) {
		return -EINVAL;
	}

	int rc = spotflow_ota_persistence_load_attempt(attempt, has_attempt);
	if (rc < 0) {
		LOG_ERR("Failed to load OTA attempt %llu results: %d",
			(unsigned long long)attempt_id, rc);
		return rc;
	}

	if (*has_attempt && attempt->attempt_id != attempt_id) {
		*has_attempt = false;
	}

	return 0;
}

int spotflow_ota_results_prepare_attempt(const struct spotflow_ota_persisted_attempt* attempt)
{
	if (attempt == NULL || attempt->attempt_id == 0 ||
	    attempt->artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	if (attempt->has_attempt_error) {
		return spotflow_ota_net_prepare_attempt_error(attempt->attempt_id,
							      attempt->attempt_error);
	}

	return spotflow_ota_net_prepare_results(attempt->attempt_id, attempt->artifact_results,
						attempt->artifact_count);
}
