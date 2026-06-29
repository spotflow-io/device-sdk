#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/core/spotflow_ota_log.h"
#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
#include "ota/firmware/spotflow_ota_fw_main.h"
#endif
#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/core/spotflow_ota_state.h"
#include "ota/core/spotflow_ota_worker.h"

LOG_MODULE_DECLARE(spotflow_ota);

#ifndef CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE
#define CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE 4096
#endif

static void ota_worker_entry(void* arg1, void* arg2, void* arg3);
static int process_worker_job(const struct spotflow_ota_worker_job* job);
static int process_rejected_attempt_job(const struct spotflow_ota_worker_job* job);
static int process_artifact_job(const struct spotflow_ota_worker_job* job);
static bool persist_and_enqueue_terminal_attempt_if_needed(void);
static bool snapshot_has_terminal_results(const struct spotflow_ota_state_snapshot* snapshot);
static int persist_snapshot_attempt(const struct spotflow_ota_state_snapshot* snapshot);
static int load_artifact_result(const struct spotflow_ota_worker_job* job,
				enum spotflow_ota_result* result);
static int persist_attempt(const struct spotflow_ota_persisted_attempt* attempt);

static K_SEM_DEFINE(ota_work_sem, 0, 1);
static K_THREAD_STACK_DEFINE(ota_worker_stack, CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE);
static struct k_thread ota_worker_thread;
static k_tid_t ota_worker_tid;

int spotflow_ota_worker_init(void)
{
	if (ota_worker_tid != NULL) {
		return 0;
	}

	ota_worker_tid = k_thread_create(
	    &ota_worker_thread, ota_worker_stack, K_THREAD_STACK_SIZEOF(ota_worker_stack),
	    ota_worker_entry, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(ota_worker_tid, "spotflow_ota");
	return 0;
}

void spotflow_ota_worker_wake(void)
{
	k_sem_give(&ota_work_sem);
}

void spotflow_ota_worker_reset(void)
{
	while (k_sem_take(&ota_work_sem, K_NO_WAIT) == 0) {
	}
}

static void ota_worker_entry(void* arg1, void* arg2, void* arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		k_sem_take(&ota_work_sem, K_FOREVER);

		for (;;) {
			struct spotflow_ota_worker_job job;

			if (!spotflow_ota_state_get_worker_job(&job)) {
				if (persist_and_enqueue_terminal_attempt_if_needed()) {
					continue;
				}
				break;
			}

			int rc = process_worker_job(&job);
			if (rc < 0) {
				LOG_ERR("OTA worker job failed for attempt %llu: %d",
					(unsigned long long)job.attempt_id, rc);
			}
		}
	}
}

static int process_worker_job(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL) {
		return -EINVAL;
	}

	switch (job->type) {
	case SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT:
		return process_rejected_attempt_job(job);
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		return process_artifact_job(job);
	case SPOTFLOW_OTA_WORKER_JOB_NONE:
	default:
		return -EINVAL;
	}
}

static int process_rejected_attempt_job(const struct spotflow_ota_worker_job* job)
{
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = job->attempt_id,
		.has_attempt_error = true,
		.attempt_error = job->attempt_error,
	};

	for (size_t i = 0; i < ARRAY_SIZE(attempt.artifact_results); i++) {
		attempt.artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}

	LOG_INF("OTA attempt %llu rejected (%s)", (unsigned long long)job->attempt_id,
		spotflow_ota_log_attempt_error_name(job->attempt_error));

	int rc = persist_attempt(&attempt);
	if (rc < 0) {
		LOG_ERR("Failed to persist rejected OTA attempt %llu: %d",
			(unsigned long long)job->attempt_id, rc);
		return rc;
	}

	rc = spotflow_ota_net_prepare_attempt_error(job->attempt_id, job->attempt_error);
	if (rc < 0) {
		LOG_ERR("Failed to queue rejected OTA attempt %llu for reporting: %d",
			(unsigned long long)job->attempt_id, rc);
	}

	return rc;
}

static int process_artifact_job(const struct spotflow_ota_worker_job* job)
{
	enum spotflow_ota_result result;
	struct spotflow_ota_state_snapshot snapshot;

	LOG_INF("OTA attempt %llu: started artifact '%s' %s (index %zu%s)",
		(unsigned long long)job->attempt_id, job->artifact.slug, job->artifact.version,
		job->artifact_index, job->artifact.is_main ? ", main" : "");

	int rc = load_artifact_result(job, &result);
	if (rc < 0) {
		LOG_ERR("Failed to resolve OTA result for attempt %llu artifact %zu: %d",
			(unsigned long long)job->attempt_id, job->artifact_index, rc);
		return rc;
	}

	LOG_INF("OTA attempt %llu: artifact '%s' %s %s", (unsigned long long)job->attempt_id,
		job->artifact.slug, job->artifact.version, spotflow_ota_log_result_name(result));

	if (result == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
		rc = spotflow_ota_persistence_save_installed_version(job->artifact.slug,
								     job->artifact.version);
		if (rc < 0) {
			LOG_ERR("Failed to persist installed version for OTA attempt %llu artifact "
				"'%s': %d",
				(unsigned long long)job->attempt_id, job->artifact.slug, rc);
			return rc;
		}
	}

	struct spotflow_ota_state_action action;
	rc = spotflow_ota_state_apply_artifact_result(job->artifact_index, result, &action);
	if (rc < 0) {
		LOG_ERR("Failed to apply OTA result for attempt %llu artifact %zu: %d",
			(unsigned long long)job->attempt_id, job->artifact_index, rc);
		return rc;
	}

	spotflow_ota_state_get_snapshot(&snapshot);
	if (snapshot.has_current_attempt && snapshot.current_attempt_id == job->attempt_id &&
	    !snapshot.has_attempt_error) {
		rc = persist_snapshot_attempt(&snapshot);
		if (rc < 0) {
			LOG_ERR("Failed to persist OTA attempt %llu results before reporting: %d",
				(unsigned long long)job->attempt_id, rc);
			return rc;
		}

		if (!snapshot.has_pending_attempt) {
			rc = spotflow_ota_net_prepare_results(snapshot.current_attempt_id,
							      snapshot.artifact_results,
							      snapshot.artifact_count);
			if (rc < 0) {
				LOG_ERR(
				    "Failed to queue OTA attempt %llu results for reporting: %d",
				    (unsigned long long)job->attempt_id, rc);
				return rc;
			}
		}
	}

	if (action.wake_worker) {
		spotflow_ota_worker_wake();
	}

	return 0;
}

static bool persist_and_enqueue_terminal_attempt_if_needed(void)
{
	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);

	if (!snapshot.has_current_attempt || snapshot.has_attempt_error ||
	    !snapshot_has_terminal_results(&snapshot)) {
		return false;
	}

	if (persist_snapshot_attempt(&snapshot) < 0) {
		LOG_ERR("Failed to persist terminal OTA attempt %llu before idle handling",
			(unsigned long long)snapshot.current_attempt_id);
		return false;
	}

	if (!snapshot.has_pending_attempt) {
		if (spotflow_ota_net_prepare_results(snapshot.current_attempt_id,
						     snapshot.artifact_results,
						     snapshot.artifact_count) < 0) {
			LOG_ERR("Failed to queue terminal OTA attempt %llu for reporting during "
				"idle handling",
				(unsigned long long)snapshot.current_attempt_id);
			return false;
		}

		return false;
	}

	LOG_DBG("OTA attempt %llu finished; promoting pending attempt %llu and discarding "
		"superseded results",
		(unsigned long long)snapshot.current_attempt_id,
		(unsigned long long)snapshot.pending_attempt_id);

	spotflow_ota_net_discard_pending();

	struct spotflow_ota_state_action action;
	int rc = spotflow_ota_state_promote_pending(&action);
	if (rc == 0 && action.wake_worker) {
		spotflow_ota_worker_wake();
	}

	return rc == 0;
}

static bool snapshot_has_terminal_results(const struct spotflow_ota_state_snapshot* snapshot)
{
	for (size_t i = 0; i < snapshot->artifact_count; i++) {
		if (snapshot->artifact_results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			return false;
		}
	}

	return true;
}

static int persist_snapshot_attempt(const struct spotflow_ota_state_snapshot* snapshot)
{
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = snapshot->current_attempt_id,
		.artifact_count = snapshot->artifact_count,
		.actionable_cancellation = snapshot->actionable_cancellation,
	};

	memcpy(attempt.artifact_results, snapshot->artifact_results,
	       sizeof(attempt.artifact_results));
	return persist_attempt(&attempt);
}

static int load_artifact_result(const struct spotflow_ota_worker_job* job,
				enum spotflow_ota_result* result)
{
	if (job == NULL || result == NULL) {
		return -EINVAL;
	}

	char installed_version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_installed_version = false;
	int rc = spotflow_ota_persistence_load_installed_version(
	    job->artifact.slug, installed_version, sizeof(installed_version),
	    &has_installed_version);
	if (rc < 0) {
		return rc;
	}

	if (has_installed_version && strcmp(installed_version, job->artifact.version) == 0) {
		LOG_INF("OTA attempt %llu: artifact '%s' already at version %s, skipping handler",
			(unsigned long long)job->attempt_id, job->artifact.slug,
			job->artifact.version);
		*result = SPOTFLOW_OTA_RESULT_SUCCEEDED;
		return 0;
	}

#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (job->artifact.is_main) {
		/*
		 * A successful main firmware update reboots before returning. Only failure and
		 * cancellation paths return to the worker.
		 */
		*result = spotflow_ota_fw_main_process_artifact(
		    job->attempt_id, job->artifact_index, &job->artifact);
		return 0;
	}
#endif

	*result = spotflow_ota_fw_custom_process_artifact(job->attempt_id, &job->artifact);
	/*
	 * The public callback contract requires a terminal result. Therefore, returning the PENDING
	 * result here is invalid and is treated as FAILED.
	 */
	if (*result == SPOTFLOW_OTA_RESULT_PENDING) {
		*result = SPOTFLOW_OTA_RESULT_FAILED;
	}

	return 0;
}

static int persist_attempt(const struct spotflow_ota_persisted_attempt* attempt)
{
	int rc = spotflow_ota_persistence_save_attempt(attempt);

	if (rc < 0) {
		LOG_ERR("Failed to save OTA attempt %llu: %d",
			(unsigned long long)attempt->attempt_id, rc);
	}

	return rc;
}
