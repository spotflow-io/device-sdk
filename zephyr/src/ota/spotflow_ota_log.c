#include <stdio.h>

#include <zephyr/logging/log.h>

#include "ota/spotflow_ota_log.h"

LOG_MODULE_DECLARE(spotflow_ota);

const char* spotflow_ota_log_result_name(enum spotflow_ota_result result)
{
	switch (result) {
	case SPOTFLOW_OTA_RESULT_SUCCEEDED:
		return "succeeded";
	case SPOTFLOW_OTA_RESULT_FAILED:
		return "failed";
	case SPOTFLOW_OTA_RESULT_CANCELED:
		return "canceled";
	case SPOTFLOW_OTA_RESULT_PENDING:
	default:
		return "pending";
	}
}

const char* spotflow_ota_log_attempt_error_name(enum spotflow_ota_attempt_error error)
{
	switch (error) {
	case SPOTFLOW_OTA_ATTEMPT_ERROR_ARTIFACT_COUNT_EXCEEDED:
		return "artifact count exceeded";
	case SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE:
		return "unknown artifact type";
	case SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE:
		return "cannot parse message";
	case SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR:
	default:
		return "unknown error";
	}
}

const char* spotflow_ota_log_phase_name(enum spotflow_ota_phase phase)
{
	switch (phase) {
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
		return "pending download";
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
		return "downloading";
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
		return "pending upgrade";
	case SPOTFLOW_OTA_PHASE_PENDING_REBOOT:
		return "pending reboot";
	case SPOTFLOW_OTA_PHASE_UNCONFIRMED:
		return "unconfirmed";
	case SPOTFLOW_OTA_PHASE_NOT_RUNNING:
	default:
		return "not running";
	}
}

void spotflow_ota_log_loaded_attempt(const struct spotflow_ota_persisted_attempt* attempt,
				     bool has_attempt)
{
	if (!has_attempt || attempt == NULL) {
		LOG_DBG("Loaded OTA persistence: no saved attempt");
		return;
	}

	if (attempt->has_attempt_error) {
		LOG_DBG("Loaded OTA attempt %llu from persistence (rejected: %s)",
			(unsigned long long)attempt->attempt_id,
			spotflow_ota_log_attempt_error_name(attempt->attempt_error));
		return;
	}

	char results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS * 12];
	size_t offset = 0;

	for (size_t i = 0; i < attempt->artifact_count; i++) {
		if (offset >= sizeof(results)) {
			break;
		}

		if (i > 0) {
			offset += snprintk(results + offset, sizeof(results) - offset, ", ");
		}

		offset += snprintk(results + offset, sizeof(results) - offset, "%s",
				   spotflow_ota_log_result_name(attempt->artifact_results[i]));
	}

	LOG_DBG("Loaded OTA attempt %llu from persistence (%zu artifacts: %s)",
		(unsigned long long)attempt->attempt_id, attempt->artifact_count, results);
}

void spotflow_ota_log_loaded_probation(const struct spotflow_ota_probation* probation,
				       bool has_probation)
{
	if (!has_probation || probation == NULL) {
		LOG_DBG("Loaded OTA persistence: no main firmware probation record");
		return;
	}

	LOG_DBG("Loaded main firmware probation for OTA attempt %llu artifact %u ('%s' %s)",
		(unsigned long long)probation->attempt_id, probation->artifact_index,
		probation->slug, probation->version);
}
