#include "ota/core/spotflow_ota_worker.h"

#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/core/spotflow_ota_log.h"
#include "ota/core/spotflow_ota_results.h"
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

enum worker_operation_type {
	WORKER_OPERATION_NONE,
	WORKER_OPERATION_REJECTED_ATTEMPT,
	WORKER_OPERATION_ARTIFACT,
	WORKER_OPERATION_REPORT_ATTEMPT,
	WORKER_OPERATION_FINALIZE_ATTEMPT,
};

enum artifact_operation_stage {
	ARTIFACT_STAGE_PERSIST_INITIAL,
	ARTIFACT_STAGE_LOAD_VERSION,
	ARTIFACT_STAGE_RUN_HANDLER,
	ARTIFACT_STAGE_STAGE_RESULT,
	ARTIFACT_STAGE_SAVE_VERSION,
	ARTIFACT_STAGE_PERSIST_RESULT,
	ARTIFACT_STAGE_CLEAR_PROBATION,
	ARTIFACT_STAGE_COMMIT_RESULT,
};

enum report_operation_stage {
	REPORT_STAGE_CAPTURE,
	REPORT_STAGE_PREPARE,
	REPORT_STAGE_PROMOTE,
};

enum worker_outcome_type {
	WORKER_OUTCOME_COMPLETE,
	WORKER_OUTCOME_RETRY,
	WORKER_OUTCOME_STALE,
	WORKER_OUTCOME_FAIL_ATTEMPT,
	WORKER_OUTCOME_INTERNAL_ERROR,
};

struct worker_outcome {
	enum worker_outcome_type type;
	int error;
};

struct worker_operation {
	bool active;
	bool continue_worker;
	enum worker_operation_type type;
	struct spotflow_ota_worker_job job;
	uint8_t retry_count;
	union {
		struct {
			enum artifact_operation_stage stage;
			enum spotflow_ota_result result;
			struct spotflow_ota_state_snapshot persisted_snapshot;
		} artifact;
		struct {
			enum report_operation_stage stage;
			struct spotflow_ota_persisted_attempt persisted_attempt;
		} report;
		struct {
			struct spotflow_ota_state_snapshot persisted_snapshot;
		} finalize;
	} data;
};

static void ota_worker_entry(void* arg1, void* arg2, void* arg3);
static void ota_worker_retry_handler(struct k_work* work);
static void initialize_worker_operation(const struct spotflow_ota_worker_job* job);
static bool initialize_finalize_operation(void);
static struct worker_outcome process_worker_operation(void);
static struct worker_outcome process_rejected_operation(void);
static struct worker_outcome process_artifact_operation(void);
static struct worker_outcome process_report_operation(void);
static struct worker_outcome process_finalize_operation(void);
static struct worker_outcome classify_state_error(const struct spotflow_ota_worker_job* job,
						  int error);
static struct worker_outcome classify_storage_error(int error, bool can_fail_attempt);
static struct worker_outcome complete_operation(void);
static struct worker_outcome retry_operation(int error);
static struct worker_outcome stale_operation(int error);
static struct worker_outcome fail_attempt(int error);
static struct worker_outcome internal_error(int error);
static int current_operation_stage(void);
static void advance_artifact_operation(enum artifact_operation_stage stage);
static void advance_report_operation(enum report_operation_stage stage);
static void schedule_worker_retry(void);
static bool snapshot_has_terminal_results(const struct spotflow_ota_state_snapshot* snapshot);
static int artifact_is_installed(const struct spotflow_ota_worker_job* job, bool* is_installed);
static enum spotflow_ota_result run_artifact_handler(const struct spotflow_ota_worker_job* job);

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
			k_mutex_lock(&worker_operation_mutex, K_FOREVER);

			if (!operation.active) {
				struct spotflow_ota_worker_job job;

				if (spotflow_ota_state_get_worker_job(&job)) {
					initialize_worker_operation(&job);
				} else if (!initialize_finalize_operation()) {
					k_mutex_unlock(&worker_operation_mutex);
					break;
				}
			}

			struct worker_outcome outcome = process_worker_operation();
			bool continue_worker = operation.continue_worker;

			switch (outcome.type) {
			case WORKER_OUTCOME_COMPLETE:
				memset(&operation, 0, sizeof(operation));
				k_mutex_unlock(&worker_operation_mutex);
				if (continue_worker) {
					continue;
				}
				break;
			case WORKER_OUTCOME_RETRY:
				LOG_ERR("OTA worker operation failed for attempt %llu at stage %d: "
					"%d; retrying",
					(unsigned long long)operation.job.attempt_id,
					current_operation_stage(), outcome.error);
				schedule_worker_retry();
				k_mutex_unlock(&worker_operation_mutex);
				break;
			case WORKER_OUTCOME_STALE:
				LOG_DBG("Dropping stale OTA worker operation for attempt %llu at "
					"stage "
					"%d: %d",
					(unsigned long long)operation.job.attempt_id,
					current_operation_stage(), outcome.error);
				memset(&operation, 0, sizeof(operation));
				k_mutex_unlock(&worker_operation_mutex);
				continue;
			case WORKER_OUTCOME_FAIL_ATTEMPT: {
				uint64_t attempt_id = operation.job.attempt_id;

				LOG_ERR("OTA attempt %llu failed due to a permanent worker error "
					"at "
					"stage %d: %d",
					(unsigned long long)attempt_id, current_operation_stage(),
					outcome.error);
				int rc = spotflow_ota_state_fail_worker_operation(attempt_id);
				memset(&operation, 0, sizeof(operation));
				k_mutex_unlock(&worker_operation_mutex);
				if (rc == 0 || rc == -ESTALE) {
					continue;
				}
				LOG_ERR("Failed to recover OTA worker state for attempt %llu: %d",
					(unsigned long long)attempt_id, rc);
				break;
			}
			case WORKER_OUTCOME_INTERNAL_ERROR:
			default:
				LOG_ERR("Stopping invalid OTA worker operation for attempt %llu at "
					"stage %d: %d",
					(unsigned long long)operation.job.attempt_id,
					current_operation_stage(), outcome.error);
				memset(&operation, 0, sizeof(operation));
				k_mutex_unlock(&worker_operation_mutex);
				break;
			}

			break;
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
		operation.type = WORKER_OPERATION_REJECTED_ATTEMPT;
		LOG_INF("OTA attempt %llu rejected (%s)", (unsigned long long)job->attempt_id,
			spotflow_ota_log_attempt_error_name(job->attempt_error));
		break;
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		operation.type = WORKER_OPERATION_ARTIFACT;
		operation.continue_worker = true;
		operation.data.artifact.stage = job->artifact_index == 0
			? ARTIFACT_STAGE_PERSIST_INITIAL
			: ARTIFACT_STAGE_LOAD_VERSION;
		LOG_INF("OTA attempt %llu: started artifact '%s' %s (index %zu%s)",
			(unsigned long long)job->attempt_id, job->artifact.slug,
			job->artifact.version, job->artifact_index,
			job->artifact.is_main ? ", main" : "");
		break;
	case SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE:
		operation.type = WORKER_OPERATION_ARTIFACT;
		operation.continue_worker = true;
		operation.data.artifact.result = job->reconciled_result;
		operation.data.artifact.stage = ARTIFACT_STAGE_STAGE_RESULT;
		LOG_INF("OTA attempt %llu: completing reconciled main firmware artifact '%s' %s "
			"(index %zu)",
			(unsigned long long)job->attempt_id, job->artifact.slug,
			job->artifact.version, job->artifact_index);
		break;
	case SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT:
		operation.type = WORKER_OPERATION_REPORT_ATTEMPT;
		operation.data.report.stage = REPORT_STAGE_CAPTURE;
		break;
	case SPOTFLOW_OTA_WORKER_JOB_NONE:
	default:
		operation.active = false;
		break;
	}
}

static bool initialize_finalize_operation(void)
{
	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);

	if (!snapshot.has_current_attempt || snapshot.artifact_result_commit_pending ||
	    (!snapshot.has_attempt_error && !snapshot_has_terminal_results(&snapshot))) {
		return false;
	}

	memset(&operation, 0, sizeof(operation));
	operation.active = true;
	operation.type = WORKER_OPERATION_FINALIZE_ATTEMPT;
	operation.job.attempt_id = snapshot.current_attempt_id;
	operation.job.generation = snapshot.current_attempt_generation;
	operation.data.finalize.persisted_snapshot = snapshot;
	return true;
}

static struct worker_outcome process_worker_operation(void)
{
	switch (operation.type) {
	case WORKER_OPERATION_REJECTED_ATTEMPT:
		return process_rejected_operation();
	case WORKER_OPERATION_ARTIFACT:
		return process_artifact_operation();
	case WORKER_OPERATION_REPORT_ATTEMPT:
		return process_report_operation();
	case WORKER_OPERATION_FINALIZE_ATTEMPT:
		return process_finalize_operation();
	case WORKER_OPERATION_NONE:
	default:
		return internal_error(-EINVAL);
	}
}

static struct worker_outcome process_rejected_operation(void)
{
	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt ||
	    snapshot.current_attempt_id != operation.job.attempt_id ||
	    snapshot.current_attempt_generation != operation.job.generation) {
		return stale_operation(-ESTALE);
	}

	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = operation.job.attempt_id,
		.has_attempt_error = true,
		.attempt_error = operation.job.attempt_error,
	};

	for (size_t i = 0; i < ARRAY_SIZE(attempt.artifact_results); i++) {
		attempt.artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}

	int rc = spotflow_ota_results_persist_attempt(&attempt);
	if (rc < 0) {
		return classify_storage_error(rc, false);
	}

	rc = spotflow_ota_state_commit_rejected_attempt(&operation.job);
	if (rc < 0) {
		return classify_state_error(&operation.job, rc);
	}

	operation.continue_worker = true;
	return complete_operation();
}

static struct worker_outcome process_artifact_operation(void)
{
	for (;;) {
		switch (operation.data.artifact.stage) {
		case ARTIFACT_STAGE_PERSIST_INITIAL: {
			struct spotflow_ota_state_snapshot snapshot;

			spotflow_ota_state_get_snapshot(&snapshot);
			if (!snapshot.has_current_attempt ||
			    snapshot.current_attempt_id != operation.job.attempt_id ||
			    snapshot.current_attempt_generation != operation.job.generation) {
				return stale_operation(-ESTALE);
			}
			if (snapshot.has_attempt_error) {
				return classify_state_error(&operation.job, -EINVAL);
			}

			int rc = spotflow_ota_results_persist_snapshot(&snapshot);
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}
			advance_artifact_operation(ARTIFACT_STAGE_LOAD_VERSION);
			break;
		}
		case ARTIFACT_STAGE_LOAD_VERSION: {
			bool is_installed;
			int rc = artifact_is_installed(&operation.job, &is_installed);
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}

			if (is_installed) {
				operation.data.artifact.result = SPOTFLOW_OTA_RESULT_SUCCEEDED;
				advance_artifact_operation(ARTIFACT_STAGE_STAGE_RESULT);
			} else {
				advance_artifact_operation(ARTIFACT_STAGE_RUN_HANDLER);
			}
			break;
		}
		case ARTIFACT_STAGE_RUN_HANDLER:
			operation.data.artifact.result = run_artifact_handler(&operation.job);
			advance_artifact_operation(ARTIFACT_STAGE_STAGE_RESULT);
			break;
		case ARTIFACT_STAGE_STAGE_RESULT: {
			LOG_INF("OTA attempt %llu: artifact '%s' %s %s",
				(unsigned long long)operation.job.attempt_id,
				operation.job.artifact.slug, operation.job.artifact.version,
				spotflow_ota_log_result_name(operation.data.artifact.result));

			int rc = spotflow_ota_state_stage_artifact_result(
				&operation.job, operation.data.artifact.result);
			if (rc < 0) {
				return classify_state_error(&operation.job, rc);
			}

#if defined(CONFIG_ZTEST)
			spotflow_ota_worker_test_after_artifact_result_applied();
#endif /* CONFIG_ZTEST */

			advance_artifact_operation(operation.data.artifact.result ==
								   SPOTFLOW_OTA_RESULT_SUCCEEDED
							   ? ARTIFACT_STAGE_SAVE_VERSION
							   : ARTIFACT_STAGE_PERSIST_RESULT);
			break;
		}
		case ARTIFACT_STAGE_SAVE_VERSION: {
			int rc = spotflow_ota_persistence_save_installed_version(
				operation.job.artifact.slug, operation.job.artifact.version);
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}
			advance_artifact_operation(ARTIFACT_STAGE_PERSIST_RESULT);
			break;
		}
		case ARTIFACT_STAGE_PERSIST_RESULT: {
			spotflow_ota_state_get_snapshot(
				&operation.data.artifact.persisted_snapshot);
			if (!operation.data.artifact.persisted_snapshot.has_current_attempt ||
			    operation.data.artifact.persisted_snapshot.current_attempt_id !=
				    operation.job.attempt_id ||
			    operation.data.artifact.persisted_snapshot.current_attempt_generation !=
				    operation.job.generation) {
				return stale_operation(-ESTALE);
			}
			if (!operation.data.artifact.persisted_snapshot
				     .artifact_result_commit_pending ||
			    operation.data.artifact.persisted_snapshot.has_attempt_error) {
				return classify_state_error(&operation.job, -EINVAL);
			}

			int rc = spotflow_ota_results_persist_snapshot(
				&operation.data.artifact.persisted_snapshot);
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}
			advance_artifact_operation(
				operation.job.type == SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE
					? ARTIFACT_STAGE_CLEAR_PROBATION
					: ARTIFACT_STAGE_COMMIT_RESULT);
			break;
		}
		case ARTIFACT_STAGE_CLEAR_PROBATION: {
			int rc = spotflow_ota_persistence_clear_probation();
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}

			spotflow_ota_state_resolve_main_firmware_probation();
			advance_artifact_operation(ARTIFACT_STAGE_COMMIT_RESULT);
			break;
		}
		case ARTIFACT_STAGE_COMMIT_RESULT: {
			int rc = spotflow_ota_state_commit_artifact_result(&operation.job);
			if (rc < 0) {
				return classify_state_error(&operation.job, rc);
			}
			return complete_operation();
		}
		default:
			return fail_attempt(-EINVAL);
		}
	}
}

static struct worker_outcome process_report_operation(void)
{
	for (;;) {
		switch (operation.data.report.stage) {
		case REPORT_STAGE_CAPTURE: {
			struct spotflow_ota_state_snapshot snapshot;
			spotflow_ota_state_get_snapshot(&snapshot);
			if (snapshot.has_current_attempt &&
			    snapshot.current_attempt_id == operation.job.attempt_id &&
			    snapshot.current_attempt_generation != operation.job.generation) {
				return stale_operation(-ESTALE);
			}

			int rc;
			if (snapshot.has_current_attempt &&
			    snapshot.current_attempt_id == operation.job.attempt_id &&
			    snapshot.current_attempt_generation == operation.job.generation) {
				if (snapshot.artifact_result_commit_pending) {
					return retry_operation(-EAGAIN);
				}

				rc = spotflow_ota_results_build_attempt(
					&snapshot, &operation.data.report.persisted_attempt);
				if (rc == 0) {
					rc = spotflow_ota_results_persist_attempt(
						&operation.data.report.persisted_attempt);
				}
				if (rc < 0) {
					struct worker_outcome outcome =
						classify_storage_error(rc, true);

					if (outcome.type != WORKER_OUTCOME_RETRY) {
						(void)spotflow_ota_state_complete_report_job(
							&operation.job, false);
					}
					return outcome;
				}
			} else {
				bool has_attempt;
				rc = spotflow_ota_results_load_attempt(
					operation.job.attempt_id,
					&operation.data.report.persisted_attempt, &has_attempt);
				if (rc < 0) {
					struct worker_outcome outcome =
						classify_storage_error(rc, false);

					if (outcome.type != WORKER_OUTCOME_RETRY) {
						(void)spotflow_ota_state_complete_report_job(
							&operation.job, false);
					}
					return outcome;
				}
				if (!has_attempt) {
					(void)spotflow_ota_state_complete_report_job(&operation.job,
										     true);
					operation.continue_worker = true;
					return complete_operation();
				}
			}

			advance_report_operation(REPORT_STAGE_PREPARE);
			break;
		}
		case REPORT_STAGE_PREPARE: {
			struct spotflow_ota_state_snapshot snapshot;
			spotflow_ota_state_get_snapshot(&snapshot);
			if (snapshot.has_current_attempt &&
			    snapshot.current_attempt_id == operation.job.attempt_id &&
			    snapshot.current_attempt_generation != operation.job.generation) {
				return stale_operation(-ESTALE);
			}
			bool is_current = snapshot.has_current_attempt &&
				snapshot.current_attempt_id == operation.job.attempt_id &&
				snapshot.current_attempt_generation == operation.job.generation;

			if (is_current && snapshot.has_pending_attempt &&
			    (snapshot.has_attempt_error ||
			     snapshot_has_terminal_results(&snapshot))) {
				advance_report_operation(REPORT_STAGE_PROMOTE);
				break;
			}

			int rc = spotflow_ota_results_prepare_attempt(
				&operation.data.report.persisted_attempt);
			bool prepared = rc == 0;
			if (rc < 0) {
				LOG_ERR("Cannot encode OTA results for attempt %llu: %d",
					(unsigned long long)operation.job.attempt_id, rc);
			}
			(void)spotflow_ota_state_complete_report_job(&operation.job, prepared);

			operation.continue_worker = !is_current ||
				(!snapshot.has_attempt_error &&
				 !snapshot_has_terminal_results(&snapshot));
			return complete_operation();
		}
		case REPORT_STAGE_PROMOTE: {
			int rc = spotflow_ota_state_promote_pending();
			if (rc == 0) {
				spotflow_ota_net_discard_pending();
				operation.continue_worker = true;
				return complete_operation();
			}

			struct spotflow_ota_state_snapshot snapshot;
			spotflow_ota_state_get_snapshot(&snapshot);
			if (!snapshot.has_current_attempt ||
			    snapshot.current_attempt_id != operation.job.attempt_id ||
			    snapshot.current_attempt_generation != operation.job.generation ||
			    !snapshot.has_pending_attempt) {
				operation.continue_worker = true;
				return stale_operation(rc);
			}
			return internal_error(rc);
		}
		default:
			return internal_error(-EINVAL);
		}
	}
}

static struct worker_outcome process_finalize_operation(void)
{
	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt ||
	    snapshot.current_attempt_id != operation.job.attempt_id ||
	    snapshot.current_attempt_generation != operation.job.generation) {
		return stale_operation(-ESTALE);
	}

	struct spotflow_ota_persisted_attempt persisted_attempt;
	int rc = spotflow_ota_results_build_attempt(&operation.data.finalize.persisted_snapshot,
						    &persisted_attempt);
	if (rc == 0) {
		rc = spotflow_ota_results_persist_attempt(&persisted_attempt);
	}
	if (rc < 0) {
		return classify_storage_error(
			rc, !operation.data.finalize.persisted_snapshot.has_attempt_error);
	}

	rc = spotflow_ota_state_commit_attempt_finalization(&operation.job);
	if (rc < 0) {
		return classify_state_error(&operation.job, rc);
	}

	operation.continue_worker = true;
	return complete_operation();
}

static struct worker_outcome classify_state_error(const struct spotflow_ota_worker_job* job,
						  int error)
{
	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);

	if (error == -ESTALE || !snapshot.has_current_attempt ||
	    snapshot.current_attempt_id != job->attempt_id ||
	    snapshot.current_attempt_generation != job->generation || snapshot.has_attempt_error) {
		return stale_operation(error);
	}

	return fail_attempt(error);
}

static struct worker_outcome classify_storage_error(int error, bool can_fail_attempt)
{
	switch (error) {
	case -EIO:
	case -EAGAIN:
	case -EBUSY:
	case -ETIMEDOUT:
		return retry_operation(error);
	default:
		return can_fail_attempt ? fail_attempt(error) : internal_error(error);
	}
}

static struct worker_outcome complete_operation(void)
{
	return (struct worker_outcome){ .type = WORKER_OUTCOME_COMPLETE };
}

static struct worker_outcome retry_operation(int error)
{
	return (struct worker_outcome){ .type = WORKER_OUTCOME_RETRY, .error = error };
}

static struct worker_outcome stale_operation(int error)
{
	return (struct worker_outcome){ .type = WORKER_OUTCOME_STALE, .error = error };
}

static struct worker_outcome fail_attempt(int error)
{
	return (struct worker_outcome){ .type = WORKER_OUTCOME_FAIL_ATTEMPT, .error = error };
}

static struct worker_outcome internal_error(int error)
{
	return (struct worker_outcome){ .type = WORKER_OUTCOME_INTERNAL_ERROR, .error = error };
}

static int current_operation_stage(void)
{
	switch (operation.type) {
	case WORKER_OPERATION_ARTIFACT:
		return operation.data.artifact.stage;
	case WORKER_OPERATION_REPORT_ATTEMPT:
		return operation.data.report.stage;
	case WORKER_OPERATION_REJECTED_ATTEMPT:
	case WORKER_OPERATION_FINALIZE_ATTEMPT:
	case WORKER_OPERATION_NONE:
	default:
		return 0;
	}
}

static void advance_artifact_operation(enum artifact_operation_stage stage)
{
	operation.data.artifact.stage = stage;
	operation.retry_count = 0;
}

static void advance_report_operation(enum report_operation_stage stage)
{
	operation.data.report.stage = stage;
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
	if (snapshot->artifact_count == 0) {
		return false;
	}

	for (size_t i = 0; i < snapshot->artifact_count; i++) {
		if (snapshot->artifact_results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			return false;
		}
	}

	return true;
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
	enum spotflow_ota_result result;

#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (job->artifact.is_main) {
		/* Success reboots before returning; only failure and cancellation return here. */
		result = spotflow_ota_fw_main_process_artifact(job->attempt_id, job->artifact_index,
							       &job->artifact);
	} else {
		result = spotflow_ota_fw_custom_process_artifact(job->attempt_id, &job->artifact);
	}
#else
	result = spotflow_ota_fw_custom_process_artifact(job->attempt_id, &job->artifact);
#endif /* CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE */

	switch (result) {
	case SPOTFLOW_OTA_RESULT_SUCCEEDED:
	case SPOTFLOW_OTA_RESULT_FAILED:
	case SPOTFLOW_OTA_RESULT_CANCELED:
		return result;
	case SPOTFLOW_OTA_RESULT_PENDING:
	default:
		LOG_ERR("Artifact handler returned invalid result %d; treating it as FAILED",
			result);
		return SPOTFLOW_OTA_RESULT_FAILED;
	}
}
