#include "ota/core/spotflow_ota_results.h"

#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

int spotflow_ota_results_build_attempt(const struct spotflow_ota_state_snapshot* snapshot,
				       struct spotflow_ota_persisted_attempt* attempt)
{
	if (snapshot == NULL || attempt == NULL || !snapshot->has_current_attempt ||
	    snapshot->current_attempt_id == 0 ||
	    snapshot->artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	*attempt = (struct spotflow_ota_persisted_attempt){
		.attempt_id = snapshot->current_attempt_id,
		.artifact_count = snapshot->artifact_count,
		.actionable_cancellation = snapshot->actionable_cancellation,
		.has_attempt_error = snapshot->has_attempt_error,
		.attempt_error = snapshot->attempt_error,
	};
	memcpy(attempt->artifact_results, snapshot->artifact_results,
	       sizeof(attempt->artifact_results));
	return 0;
}

int spotflow_ota_results_persist_snapshot(const struct spotflow_ota_state_snapshot* snapshot)
{
	struct spotflow_ota_persisted_attempt attempt;
	int rc = spotflow_ota_results_build_attempt(snapshot, &attempt);

	return rc < 0 ? rc : spotflow_ota_results_persist_attempt(&attempt);
}

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
