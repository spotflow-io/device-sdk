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

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

#ifndef CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE
#define CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE 4096
#endif /* CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE */

#define OTA_WORKER_RETRY_INITIAL_DELAY_MS 20U
#define OTA_WORKER_RETRY_MAX_DELAY_MS (5U * 60U * 1000U)
#define OTA_WORKER_RETRY_MAX_SHIFT 14U

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
			bool probation_resolved;
			struct spotflow_ota_persistence_capture persistence_capture;
		} artifact;
		struct {
			enum report_operation_stage stage;
			struct spotflow_ota_persisted_attempt persisted_attempt;
		} report;
		struct {
			bool attempt_captured;
			struct spotflow_ota_persisted_attempt persisted_attempt;
		} finalize;
	} data;
};

static void ota_worker_entry(void* arg1, void* arg2, void* arg3);
static void ota_worker_retry_handler(struct k_work* work);
static void initialize_worker_operation(const struct spotflow_ota_worker_job* job);
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
static const struct spotflow_ota_artifact*
worker_job_artifact(const struct spotflow_ota_worker_job* job);
static int artifact_is_installed(const struct spotflow_ota_worker_job* job, bool* is_installed);
static enum spotflow_ota_result run_artifact_handler(const struct spotflow_ota_worker_job* job);

#if defined(CONFIG_ZTEST)
void __weak spotflow_ota_worker_test_after_artifact_result_applied(void) {}
void __weak spotflow_ota_worker_test_after_artifact_result_persisted(void) {}
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
				} else {
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
					(unsigned long long)operation.job.token.attempt_id,
					current_operation_stage(), outcome.error);
				schedule_worker_retry();
				k_mutex_unlock(&worker_operation_mutex);
				break;
			case WORKER_OUTCOME_STALE:
				LOG_DBG("Dropping stale OTA worker operation for attempt %llu at "
					"stage "
					"%d: %d",
					(unsigned long long)operation.job.token.attempt_id,
					current_operation_stage(), outcome.error);
				memset(&operation, 0, sizeof(operation));
				k_mutex_unlock(&worker_operation_mutex);
				continue;
			case WORKER_OUTCOME_FAIL_ATTEMPT: {
				const struct spotflow_ota_operation_token token =
					operation.job.token;

				LOG_ERR("OTA attempt %llu failed due to a permanent worker error "
					"at "
					"stage %d: %d",
					(unsigned long long)token.attempt_id,
					current_operation_stage(), outcome.error);
				int rc = spotflow_ota_state_fail_worker_operation(&token);
				memset(&operation, 0, sizeof(operation));
				k_mutex_unlock(&worker_operation_mutex);
				if (rc == 0 || rc == -ESTALE) {
					continue;
				}
				LOG_ERR("Failed to recover OTA worker state for attempt %llu: %d",
					(unsigned long long)token.attempt_id, rc);
				break;
			}
			case WORKER_OUTCOME_INTERNAL_ERROR:
			default:
				LOG_ERR("Stopping invalid OTA worker operation for attempt %llu at "
					"stage %d: %d",
					(unsigned long long)operation.job.token.attempt_id,
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
		LOG_INF("OTA attempt %llu rejected (%s)", (unsigned long long)job->token.attempt_id,
			spotflow_ota_log_attempt_error_name(job->data.rejected_attempt.error));
		break;
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		operation.type = WORKER_OPERATION_ARTIFACT;
		operation.continue_worker = true;
		operation.data.artifact.stage = job->data.process_artifact.artifact_index == 0
			? ARTIFACT_STAGE_PERSIST_INITIAL
			: ARTIFACT_STAGE_LOAD_VERSION;
		LOG_INF("OTA attempt %llu: started artifact '%s' %s (index %zu%s)",
			(unsigned long long)job->token.attempt_id,
			job->data.process_artifact.artifact.slug,
			job->data.process_artifact.artifact.version,
			job->data.process_artifact.artifact_index,
			job->data.process_artifact.artifact.is_main ? ", main" : "");
		break;
	case SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE:
		operation.type = WORKER_OPERATION_ARTIFACT;
		operation.continue_worker = true;
		operation.data.artifact.result = job->data.complete_main_firmware.result;
		operation.data.artifact.stage = ARTIFACT_STAGE_STAGE_RESULT;
		LOG_INF("OTA attempt %llu: completing reconciled main firmware artifact '%s' %s "
			"(index %zu)",
			(unsigned long long)job->token.attempt_id,
			job->data.complete_main_firmware.artifact.slug,
			job->data.complete_main_firmware.artifact.version,
			job->data.complete_main_firmware.artifact_index);
		break;
	case SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT:
		operation.type = WORKER_OPERATION_REPORT_ATTEMPT;
		operation.data.report.stage = REPORT_STAGE_CAPTURE;
		break;
	case SPOTFLOW_OTA_WORKER_JOB_FINALIZE_ATTEMPT:
		operation.type = WORKER_OPERATION_FINALIZE_ATTEMPT;
		break;
	case SPOTFLOW_OTA_WORKER_JOB_NONE:
	default:
		operation.active = false;
		break;
	}
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
	struct spotflow_ota_persistence_capture capture;
	int rc = spotflow_ota_state_capture_persisted_attempt(
		&operation.job, SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE, &capture);
	if (rc < 0) {
		return classify_state_error(&operation.job, rc);
	}

	rc = spotflow_ota_results_persist_attempt(&capture.attempt);
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
			struct spotflow_ota_persistence_capture capture;
			int rc = spotflow_ota_state_capture_persisted_attempt(
				&operation.job, SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE, &capture);
			if (rc < 0) {
				return classify_state_error(&operation.job, rc);
			}
			rc = spotflow_ota_results_persist_attempt(&capture.attempt);
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
			const struct spotflow_ota_artifact* artifact =
				worker_job_artifact(&operation.job);

			LOG_INF("OTA attempt %llu: artifact '%s' %s %s",
				(unsigned long long)operation.job.token.attempt_id, artifact->slug,
				artifact->version,
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
			const struct spotflow_ota_artifact* artifact =
				worker_job_artifact(&operation.job);
			int rc = spotflow_ota_persistence_save_installed_version(artifact->slug,
										 artifact->version);
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}
			advance_artifact_operation(ARTIFACT_STAGE_PERSIST_RESULT);
			break;
		}
		case ARTIFACT_STAGE_PERSIST_RESULT: {
			int rc = spotflow_ota_state_capture_persisted_attempt(
				&operation.job, SPOTFLOW_OTA_PERSISTENCE_VIEW_STAGED_RESULT,
				&operation.data.artifact.persistence_capture);
			if (rc < 0) {
				return classify_state_error(&operation.job, rc);
			}

			rc = spotflow_ota_results_persist_attempt(
				&operation.data.artifact.persistence_capture.attempt);
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}

#if defined(CONFIG_ZTEST)
			spotflow_ota_worker_test_after_artifact_result_persisted();
#endif /* CONFIG_ZTEST */

			advance_artifact_operation(
				operation.job.type == SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE &&
						!operation.data.artifact.probation_resolved
					? ARTIFACT_STAGE_CLEAR_PROBATION
					: ARTIFACT_STAGE_COMMIT_RESULT);
			break;
		}
		case ARTIFACT_STAGE_CLEAR_PROBATION: {
			int rc = spotflow_ota_persistence_clear_probation();
			if (rc < 0) {
				return classify_storage_error(rc, true);
			}

			rc = spotflow_ota_state_commit_main_firmware_probation_cleared(
				&operation.job.token);
			if (rc < 0) {
				return classify_state_error(&operation.job, rc);
			}
			operation.data.artifact.probation_resolved = true;
			advance_artifact_operation(ARTIFACT_STAGE_COMMIT_RESULT);
			break;
		}
		case ARTIFACT_STAGE_COMMIT_RESULT: {
			int rc = spotflow_ota_state_commit_artifact_result(
				&operation.job,
				operation.data.artifact.persistence_capture.mutation_revision);
			if (rc == -EAGAIN) {
				advance_artifact_operation(ARTIFACT_STAGE_PERSIST_RESULT);
				break;
			}
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
			struct spotflow_ota_persistence_capture capture;
			int rc = spotflow_ota_state_capture_persisted_attempt(
				&operation.job, SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE, &capture);
			if (rc == 0) {
				operation.data.report.persisted_attempt = capture.attempt;
				rc = spotflow_ota_results_persist_attempt(
					&operation.data.report.persisted_attempt);
				if (rc < 0) {
					struct worker_outcome outcome =
						classify_storage_error(rc, true);

					if (outcome.type != WORKER_OUTCOME_RETRY) {
						(void)spotflow_ota_state_complete_report_job(
							&operation.job, false);
					}
					return outcome;
				}
			} else if (rc == -ENOENT) {
				bool has_attempt;
				rc = spotflow_ota_results_load_attempt(
					operation.job.token.attempt_id,
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
			} else {
				return classify_state_error(&operation.job, rc);
			}

			advance_report_operation(REPORT_STAGE_PREPARE);
			break;
		}
		case REPORT_STAGE_PREPARE: {
			struct spotflow_ota_report_plan plan;
			int rc = spotflow_ota_state_get_report_plan(&operation.job, &plan);
			if (rc < 0) {
				return classify_state_error(&operation.job, rc);
			}

			if (plan.promote_pending) {
				advance_report_operation(REPORT_STAGE_PROMOTE);
				break;
			}

			rc = spotflow_ota_results_prepare_attempt(
				&operation.data.report.persisted_attempt);
			bool prepared = rc == 0;
			if (rc < 0) {
				LOG_ERR("Cannot encode OTA results for attempt %llu: %d",
					(unsigned long long)operation.job.token.attempt_id, rc);
			}
			(void)spotflow_ota_state_complete_report_job(&operation.job, prepared);

			operation.continue_worker = plan.continue_worker;
			return complete_operation();
		}
		case REPORT_STAGE_PROMOTE: {
			int rc = spotflow_ota_state_promote_pending(&operation.job);
			if (rc == 0) {
				spotflow_ota_net_discard_pending();
				operation.continue_worker = true;
				return complete_operation();
			}

			if (rc == -ESTALE) {
				operation.continue_worker = true;
				return stale_operation(-ESTALE);
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
	if (!operation.data.finalize.attempt_captured) {
		struct spotflow_ota_persistence_capture capture;
		int rc = spotflow_ota_state_capture_persisted_attempt(
			&operation.job, SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE, &capture);
		if (rc < 0) {
			return classify_state_error(&operation.job, rc);
		}
		operation.data.finalize.persisted_attempt = capture.attempt;
		operation.data.finalize.attempt_captured = true;
	}

	int rc = spotflow_ota_results_persist_attempt(&operation.data.finalize.persisted_attempt);
	if (rc < 0) {
		return classify_storage_error(
			rc, !operation.data.finalize.persisted_attempt.has_attempt_error);
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
	if (error == -ESTALE || error == -ENOENT ||
	    spotflow_ota_state_validate_operation(&job->token) < 0) {
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
	uint32_t delay_ms =
		MIN(OTA_WORKER_RETRY_INITIAL_DELAY_MS << shift, OTA_WORKER_RETRY_MAX_DELAY_MS);

	if (operation.retry_count < UINT8_MAX) {
		operation.retry_count++;
	}

	(void)k_work_reschedule(&ota_worker_retry, K_MSEC(delay_ms));
}

static const struct spotflow_ota_artifact*
worker_job_artifact(const struct spotflow_ota_worker_job* job)
{
	switch (job->type) {
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		return &job->data.process_artifact.artifact;
	case SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE:
		return &job->data.complete_main_firmware.artifact;
	default:
		__ASSERT_NO_MSG(false);
		return NULL;
	}
}

static int artifact_is_installed(const struct spotflow_ota_worker_job* job, bool* is_installed)
{
	const struct spotflow_ota_artifact* artifact = worker_job_artifact(job);
	char installed_version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_installed_version = false;
	int rc = spotflow_ota_persistence_load_installed_version(artifact->slug, installed_version,
								 sizeof(installed_version),
								 &has_installed_version);
	if (rc < 0) {
		return rc;
	}

	*is_installed = has_installed_version && strcmp(installed_version, artifact->version) == 0;
	if (*is_installed) {
		LOG_INF("OTA attempt %llu: artifact '%s' already at version %s, skipping handler",
			(unsigned long long)job->token.attempt_id, artifact->slug,
			artifact->version);
	}

	return 0;
}

static enum spotflow_ota_result run_artifact_handler(const struct spotflow_ota_worker_job* job)
{
	const struct spotflow_ota_artifact* artifact = worker_job_artifact(job);
	enum spotflow_ota_result result;

#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (artifact->is_main) {
		/* Success reboots before returning; only failure and cancellation return here. */
		result = spotflow_ota_fw_main_process_artifact(job);
	} else {
		result = spotflow_ota_fw_custom_process_artifact(job->token.attempt_id, artifact);
	}
#else
	result = spotflow_ota_fw_custom_process_artifact(job->token.attempt_id, artifact);
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
