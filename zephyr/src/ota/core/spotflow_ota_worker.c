#include "ota/core/spotflow_ota_worker.h"

#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/core/spotflow_ota_log.h"
#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
#include "ota/firmware/spotflow_ota_fw_main.h"
#endif /* CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE */
#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/core/spotflow_ota_state.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#ifndef CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE
#define CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE 4096
#endif /* CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE */

#define OTA_WORKER_RETRY_INITIAL_DELAY_MS 20U
#define OTA_WORKER_RETRY_MAX_SHIFT 6U

enum worker_operation_stage {
	WORKER_STAGE_NONE,
	WORKER_STAGE_REJECT_PERSIST,
	WORKER_STAGE_REJECT_REPORT,
	WORKER_STAGE_ARTIFACT_PERSIST_INITIAL,
	WORKER_STAGE_ARTIFACT_LOAD_VERSION,
	WORKER_STAGE_ARTIFACT_RUN_HANDLER,
	WORKER_STAGE_ARTIFACT_STAGE_RESULT,
	WORKER_STAGE_ARTIFACT_SAVE_VERSION,
	WORKER_STAGE_ARTIFACT_PERSIST_RESULT,
	WORKER_STAGE_ARTIFACT_COMMIT_RESULT,
	WORKER_STAGE_ARTIFACT_REPORT_RESULT,
};

struct worker_operation {
	bool active;
	struct spotflow_ota_worker_job job;
	enum worker_operation_stage stage;
	enum spotflow_ota_result result;
	struct spotflow_ota_state_snapshot persisted_snapshot;
	uint8_t retry_count;
};

static void ota_worker_entry(void* arg1, void* arg2, void* arg3);
static void ota_worker_retry_handler(struct k_work* work);
static void initialize_worker_operation(const struct spotflow_ota_worker_job* job);
static int process_worker_operation(void);
static int process_rejected_operation(void);
static int process_artifact_operation(void);
static int process_idle_terminal_attempt(void);
static void advance_operation(enum worker_operation_stage stage);
static void schedule_worker_retry(void);
static bool snapshot_has_terminal_results(const struct spotflow_ota_state_snapshot* snapshot);
static int persist_snapshot_attempt(const struct spotflow_ota_state_snapshot* snapshot);
static int artifact_is_installed(const struct spotflow_ota_worker_job* job, bool* is_installed);
static enum spotflow_ota_result run_artifact_handler(const struct spotflow_ota_worker_job* job);
static int persist_attempt(const struct spotflow_ota_persisted_attempt* attempt);

#if defined(CONFIG_ZTEST)
void __weak spotflow_ota_worker_test_after_artifact_result_applied(void) {}
#endif /* CONFIG_ZTEST */

static K_SEM_DEFINE(ota_work_sem, 0, 1);
static K_MUTEX_DEFINE(worker_operation_mutex);
static K_WORK_DELAYABLE_DEFINE(ota_worker_retry, ota_worker_retry_handler);
static K_THREAD_STACK_DEFINE(ota_worker_stack, CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE);
static struct k_thread ota_worker_thread;
static k_tid_t ota_worker_tid;
static struct worker_operation operation;

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
	k_mutex_lock(&worker_operation_mutex, K_FOREVER);
	(void)k_work_cancel_delayable(&ota_worker_retry);
	memset(&operation, 0, sizeof(operation));
	while (k_sem_take(&ota_work_sem, K_NO_WAIT) == 0) {
	}
	k_mutex_unlock(&worker_operation_mutex);
}

static void ota_worker_entry(void* arg1, void* arg2, void* arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		k_sem_take(&ota_work_sem, K_FOREVER);
		(void)k_work_cancel_delayable(&ota_worker_retry);

		for (;;) {
			int rc;

			k_mutex_lock(&worker_operation_mutex, K_FOREVER);

			if (!operation.active) {
				struct spotflow_ota_worker_job job;

				if (!spotflow_ota_state_get_worker_job(&job)) {
					rc = process_idle_terminal_attempt();
					k_mutex_unlock(&worker_operation_mutex);

					if (rc > 0) {
						continue;
					}
					if (rc < 0) {
						schedule_worker_retry();
					}
					break;
				}

				initialize_worker_operation(&job);
			}

			rc = process_worker_operation();
			if (rc < 0) {
				LOG_ERR("OTA worker operation failed for attempt %llu at stage %d: "
					"%d",
					(unsigned long long)operation.job.attempt_id,
					operation.stage, rc);
				schedule_worker_retry();
				k_mutex_unlock(&worker_operation_mutex);
				break;
			}

			memset(&operation, 0, sizeof(operation));
			k_mutex_unlock(&worker_operation_mutex);
		}
	}
}

static void ota_worker_retry_handler(struct k_work* work)
{
	ARG_UNUSED(work);
	spotflow_ota_worker_wake();
}

static void initialize_worker_operation(const struct spotflow_ota_worker_job* job)
{
	memset(&operation, 0, sizeof(operation));
	operation.active = true;
	operation.job = *job;

	switch (job->type) {
	case SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT:
		operation.stage = WORKER_STAGE_REJECT_PERSIST;
		LOG_INF("OTA attempt %llu rejected (%s)", (unsigned long long)job->attempt_id,
			spotflow_ota_log_attempt_error_name(job->attempt_error));
		break;
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		operation.stage = job->artifact_index == 0 ? WORKER_STAGE_ARTIFACT_PERSIST_INITIAL
							   : WORKER_STAGE_ARTIFACT_LOAD_VERSION;
		LOG_INF("OTA attempt %llu: started artifact '%s' %s (index %zu%s)",
			(unsigned long long)job->attempt_id, job->artifact.slug,
			job->artifact.version, job->artifact_index,
			job->artifact.is_main ? ", main" : "");
		break;
	case SPOTFLOW_OTA_WORKER_JOB_NONE:
	default:
		operation.active = false;
		operation.stage = WORKER_STAGE_NONE;
		break;
	}
}

static int process_worker_operation(void)
{
	switch (operation.job.type) {
	case SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT:
		return process_rejected_operation();
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		return process_artifact_operation();
	case SPOTFLOW_OTA_WORKER_JOB_NONE:
	default:
		return -EINVAL;
	}
}

static int process_rejected_operation(void)
{
	for (;;) {
		switch (operation.stage) {
		case WORKER_STAGE_REJECT_PERSIST: {
			struct spotflow_ota_persisted_attempt attempt = {
				.attempt_id = operation.job.attempt_id,
				.has_attempt_error = true,
				.attempt_error = operation.job.attempt_error,
			};

			for (size_t i = 0; i < ARRAY_SIZE(attempt.artifact_results); i++) {
				attempt.artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
			}

			int rc = persist_attempt(&attempt);
			if (rc < 0) {
				return rc;
			}
			advance_operation(WORKER_STAGE_REJECT_REPORT);
			break;
		}
		case WORKER_STAGE_REJECT_REPORT: {
			int rc = spotflow_ota_net_prepare_attempt_error(
				operation.job.attempt_id, operation.job.attempt_error);
			if (rc < 0) {
				return rc;
			}
			return 0;
		}
		default:
			return -EINVAL;
		}
	}
}

static int process_artifact_operation(void)
{
	for (;;) {
		switch (operation.stage) {
		case WORKER_STAGE_ARTIFACT_PERSIST_INITIAL: {
			struct spotflow_ota_state_snapshot snapshot;

			spotflow_ota_state_get_snapshot(&snapshot);
			if (!snapshot.has_current_attempt ||
			    snapshot.current_attempt_id != operation.job.attempt_id ||
			    snapshot.has_attempt_error) {
				return -EINVAL;
			}

			int rc = persist_snapshot_attempt(&snapshot);
			if (rc < 0) {
				return rc;
			}
			advance_operation(WORKER_STAGE_ARTIFACT_LOAD_VERSION);
			break;
		}
		case WORKER_STAGE_ARTIFACT_LOAD_VERSION: {
			bool is_installed;
			int rc = artifact_is_installed(&operation.job, &is_installed);
			if (rc < 0) {
				return rc;
			}

			if (is_installed) {
				operation.result = SPOTFLOW_OTA_RESULT_SUCCEEDED;
				advance_operation(WORKER_STAGE_ARTIFACT_STAGE_RESULT);
			} else {
				advance_operation(WORKER_STAGE_ARTIFACT_RUN_HANDLER);
			}
			break;
		}
		case WORKER_STAGE_ARTIFACT_RUN_HANDLER:
			operation.result = run_artifact_handler(&operation.job);
			advance_operation(WORKER_STAGE_ARTIFACT_STAGE_RESULT);
			break;
		case WORKER_STAGE_ARTIFACT_STAGE_RESULT: {
			LOG_INF("OTA attempt %llu: artifact '%s' %s %s",
				(unsigned long long)operation.job.attempt_id,
				operation.job.artifact.slug, operation.job.artifact.version,
				spotflow_ota_log_result_name(operation.result));

			int rc = spotflow_ota_state_stage_artifact_result(
				operation.job.attempt_id, operation.job.artifact_index,
				operation.result);
			if (rc < 0) {
				return rc;
			}

#if defined(CONFIG_ZTEST)
			spotflow_ota_worker_test_after_artifact_result_applied();
#endif /* CONFIG_ZTEST */

			advance_operation(operation.result == SPOTFLOW_OTA_RESULT_SUCCEEDED
						  ? WORKER_STAGE_ARTIFACT_SAVE_VERSION
						  : WORKER_STAGE_ARTIFACT_PERSIST_RESULT);
			break;
		}
		case WORKER_STAGE_ARTIFACT_SAVE_VERSION: {
			int rc = spotflow_ota_persistence_save_installed_version(
				operation.job.artifact.slug, operation.job.artifact.version);
			if (rc < 0) {
				return rc;
			}
			advance_operation(WORKER_STAGE_ARTIFACT_PERSIST_RESULT);
			break;
		}
		case WORKER_STAGE_ARTIFACT_PERSIST_RESULT: {
			spotflow_ota_state_get_snapshot(&operation.persisted_snapshot);
			if (!operation.persisted_snapshot.has_current_attempt ||
			    operation.persisted_snapshot.current_attempt_id !=
				    operation.job.attempt_id ||
			    !operation.persisted_snapshot.artifact_result_commit_pending ||
			    operation.persisted_snapshot.has_attempt_error) {
				return -EINVAL;
			}

			int rc = persist_snapshot_attempt(&operation.persisted_snapshot);
			if (rc < 0) {
				return rc;
			}
			advance_operation(WORKER_STAGE_ARTIFACT_COMMIT_RESULT);
			break;
		}
		case WORKER_STAGE_ARTIFACT_COMMIT_RESULT: {
			struct spotflow_ota_state_action action;
			int rc = spotflow_ota_state_commit_artifact_result(
				operation.job.attempt_id, operation.job.artifact_index, &action);
			if (rc < 0) {
				return rc;
			}

			advance_operation(WORKER_STAGE_ARTIFACT_REPORT_RESULT);
			break;
		}
		case WORKER_STAGE_ARTIFACT_REPORT_RESULT: {
			struct spotflow_ota_state_snapshot current_snapshot;

			spotflow_ota_state_get_snapshot(&current_snapshot);
			if (!current_snapshot.has_current_attempt ||
			    current_snapshot.current_attempt_id != operation.job.attempt_id ||
			    current_snapshot.has_pending_attempt) {
				return 0;
			}

			int rc = spotflow_ota_net_prepare_results(
				operation.persisted_snapshot.current_attempt_id,
				operation.persisted_snapshot.artifact_results,
				operation.persisted_snapshot.artifact_count);
			if (rc < 0) {
				return rc;
			}
			return 0;
		}
		default:
			return -EINVAL;
		}
	}
}

static int process_idle_terminal_attempt(void)
{
	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);

	if (!snapshot.has_current_attempt || snapshot.has_attempt_error ||
	    snapshot.artifact_result_commit_pending || !snapshot_has_terminal_results(&snapshot)) {
		return 0;
	}

	int rc = persist_snapshot_attempt(&snapshot);
	if (rc < 0) {
		LOG_ERR("Failed to persist terminal OTA attempt %llu before idle handling",
			(unsigned long long)snapshot.current_attempt_id);
		return rc;
	}

	if (!snapshot.has_pending_attempt) {
		rc = spotflow_ota_net_prepare_results(snapshot.current_attempt_id,
						      snapshot.artifact_results,
						      snapshot.artifact_count);
		if (rc < 0) {
			LOG_ERR("Failed to queue terminal OTA attempt %llu during idle handling",
				(unsigned long long)snapshot.current_attempt_id);
		}
		return rc;
	}

	LOG_DBG("OTA attempt %llu finished; promoting pending attempt %llu and discarding "
		"superseded results",
		(unsigned long long)snapshot.current_attempt_id,
		(unsigned long long)snapshot.pending_attempt_id);
	spotflow_ota_net_discard_pending();

	struct spotflow_ota_state_action action;
	rc = spotflow_ota_state_promote_pending(&action);
	return rc < 0 ? rc : 1;
}

static void advance_operation(enum worker_operation_stage stage)
{
	operation.stage = stage;
	operation.retry_count = 0;
}

static void schedule_worker_retry(void)
{
	uint8_t shift = MIN(operation.retry_count, OTA_WORKER_RETRY_MAX_SHIFT);
	uint32_t delay_ms = OTA_WORKER_RETRY_INITIAL_DELAY_MS << shift;

	if (operation.retry_count < UINT8_MAX) {
		operation.retry_count++;
	}

	(void)k_work_reschedule(&ota_worker_retry, K_MSEC(delay_ms));
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

static int artifact_is_installed(const struct spotflow_ota_worker_job* job, bool* is_installed)
{
	char installed_version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_installed_version = false;
	int rc = spotflow_ota_persistence_load_installed_version(
		job->artifact.slug, installed_version, sizeof(installed_version),
		&has_installed_version);
	if (rc < 0) {
		return rc;
	}

	*is_installed =
		has_installed_version && strcmp(installed_version, job->artifact.version) == 0;
	if (*is_installed) {
		LOG_INF("OTA attempt %llu: artifact '%s' already at version %s, skipping handler",
			(unsigned long long)job->attempt_id, job->artifact.slug,
			job->artifact.version);
	}

	return 0;
}

static enum spotflow_ota_result run_artifact_handler(const struct spotflow_ota_worker_job* job)
{
#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (job->artifact.is_main) {
		/* Success reboots before returning; only failure and cancellation return here. */
		return spotflow_ota_fw_main_process_artifact(job->attempt_id, job->artifact_index,
							     &job->artifact);
	}
#endif /* CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE */

	enum spotflow_ota_result result =
		spotflow_ota_fw_custom_process_artifact(job->attempt_id, &job->artifact);
	return result == SPOTFLOW_OTA_RESULT_PENDING ? SPOTFLOW_OTA_RESULT_FAILED : result;
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
