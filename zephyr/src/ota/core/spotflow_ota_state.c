#include "ota/core/spotflow_ota_state.h"

#include "ota/persistence/spotflow_ota_records_cbor.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

enum attempt_lifecycle {
	ATTEMPT_LIFECYCLE_EMPTY,
	ATTEMPT_LIFECYCLE_AWAITING_MANIFEST,
	ATTEMPT_LIFECYCLE_ACTIVE,
	ATTEMPT_LIFECYCLE_FINALIZING,
	ATTEMPT_LIFECYCLE_TERMINAL,
	ATTEMPT_LIFECYCLE_REJECTING,
	ATTEMPT_LIFECYCLE_REJECTION_CLAIMED,
	ATTEMPT_LIFECYCLE_REJECTED,
};

struct attempt_identity {
	enum attempt_lifecycle lifecycle;
	uint64_t id;
	uint32_t generation;
};

enum artifact_plan_source {
	ARTIFACT_PLAN_NONE,
	ARTIFACT_PLAN_PROBATION_PREFIX,
	ARTIFACT_PLAN_PERSISTED_RESULTS,
	ARTIFACT_PLAN_FULL_MANIFEST,
};

enum artifact_transaction_state {
	ARTIFACT_TRANSACTION_IDLE,
	ARTIFACT_TRANSACTION_RUNNING,
	ARTIFACT_TRANSACTION_RESULT_STAGED,
};

struct artifact_transaction {
	enum artifact_transaction_state state;
	size_t artifact_index;
};

struct artifact_plan {
	enum artifact_plan_source source;
	size_t count;
	struct spotflow_ota_artifact artifacts[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	enum spotflow_ota_result results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
};

enum artifact_sequence_policy {
	ARTIFACT_SEQUENCE_CONTINUE,
	ARTIFACT_SEQUENCE_STOP_REMAINING,
};

struct artifact_execution {
	size_t next_index;
	struct artifact_transaction transaction;
	bool cancellation_requested;
	enum artifact_sequence_policy sequence_policy;
};

enum report_state {
	REPORT_STATE_IDLE,
	REPORT_STATE_REQUESTED,
	REPORT_STATE_CLAIMED,
	REPORT_STATE_CLAIMED_RERUN_REQUESTED,
	REPORT_STATE_BLOCKED,
};

enum main_upgrade_state {
	MAIN_UPGRADE_IDLE,
	MAIN_UPGRADE_HANDLER_ACTIVE,
	MAIN_UPGRADE_COMMITTING,
	MAIN_UPGRADE_REBOOT_READY,
	MAIN_UPGRADE_REBOOT_STARTED,
};

enum main_probation_state {
	MAIN_PROBATION_NONE,
	MAIN_PROBATION_PENDING,
	MAIN_PROBATION_COMPLETION_QUEUED,
	MAIN_PROBATION_COMPLETION_CLAIMED,
};

enum pending_attempt_kind {
	PENDING_ATTEMPT_NONE,
	PENDING_ATTEMPT_UPDATE,
	PENDING_ATTEMPT_REJECTION,
};

struct pending_attempt_state {
	enum pending_attempt_kind kind;
	struct spotflow_ota_update_msg update;
	enum spotflow_ota_attempt_error rejection_error;
};

enum attempt_failure_state {
	ATTEMPT_FAILURE_NONE,
	ATTEMPT_FAILURE_PRESENT,
};

struct attempt_failure {
	enum attempt_failure_state state;
	enum spotflow_ota_attempt_error error;
};

enum main_firmware_presence {
	MAIN_FIRMWARE_ABSENT,
	MAIN_FIRMWARE_PRESENT,
};

struct main_firmware_probation {
	enum main_probation_state state;
	enum spotflow_ota_result reconciled_result;
};

struct main_firmware_context {
	enum main_firmware_presence presence;
	size_t artifact_index;
	struct spotflow_ota_artifact artifact;
	struct spotflow_ota_main_firmware_state status;
	bool abort_requested;
	enum main_upgrade_state upgrade;
	struct main_firmware_probation probation;
};

struct attempt_state {
	struct attempt_identity identity;
	struct artifact_plan plan;
	struct artifact_execution execution;
	struct attempt_failure failure;
	struct main_firmware_context main_firmware;
};

struct ota_state_store {
	struct attempt_state current_attempt;
	struct pending_attempt_state pending_attempt;
	enum report_state report_state;
	uint32_t next_attempt_generation;
};

static struct ota_state_store ota_state;
static K_MUTEX_DEFINE(state_mutex);

static void clear_worker_job(struct spotflow_ota_worker_job* job);
static void clear_attempt(struct attempt_state* attempt);
static uint32_t allocate_attempt_generation(void);
static bool attempt_exists(const struct attempt_state* attempt);
static bool artifact_transaction_is_active(const struct attempt_state* attempt);
static bool artifact_result_commit_is_pending(const struct attempt_state* attempt);
static bool main_probation_is_pending(const struct attempt_state* attempt);
static bool main_upgrade_is_irreversible(const struct attempt_state* attempt);
static bool state_is_valid(void);
static void assert_state_locked(void);
static void request_report(void);
static void refresh_attempt_lifecycle(struct attempt_state* attempt);
static void
restore_main_firmware_artifact_from_probation(const struct spotflow_ota_probation* probation);
static int validate_update_msg(const struct spotflow_ota_update_msg* msg);
static bool validate_artifact(const struct spotflow_ota_artifact* artifact);
static void start_attempt(const struct spotflow_ota_update_msg* msg, struct attempt_state* attempt);
static int rehydrate_attempt(const struct spotflow_ota_update_msg* msg,
			     struct attempt_state* attempt,
			     struct spotflow_ota_update_result* result);
static void start_rejected_attempt(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				   struct attempt_state* attempt);
static void clear_pending_attempt(void);
static void store_pending_manifest(const struct spotflow_ota_update_msg* msg);
static void store_pending_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error);
static void apply_immediate_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				      struct spotflow_ota_rejection_result* result);
static spotflow_ota_state_effects supersede_current_for_pending(void);
static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source);
static bool attempt_has_terminal_results(const struct attempt_state* attempt);
static bool attempt_has_reportable_results(const struct attempt_state* attempt);
static bool attempt_has_succeeded_artifact(const struct attempt_state* attempt);
static bool attempt_has_runnable_artifact(const struct attempt_state* attempt);
static void set_artifact_result(struct attempt_state* attempt, size_t artifact_index,
				enum spotflow_ota_result result);
static spotflow_ota_state_effects artifact_result_effects(void);
static bool main_firmware_phase_allows_pause(enum spotflow_ota_phase phase);
static bool main_firmware_phase_allows_abort(enum spotflow_ota_phase phase);
static void cancel_pending_artifacts(struct attempt_state* attempt);
static void advance_current_artifact(struct attempt_state* attempt);

void spotflow_ota_state_reset(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	clear_attempt(&ota_state.current_attempt);
	clear_pending_attempt();
	ota_state.report_state = REPORT_STATE_IDLE;
	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_init_from_persistence(const struct spotflow_ota_persisted_attempt* attempt,
					     bool has_attempt,
					     const struct spotflow_ota_probation* probation,
					     bool has_probation)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	clear_attempt(&ota_state.current_attempt);
	clear_pending_attempt();
	ota_state.report_state = REPORT_STATE_IDLE;

	if (has_attempt && attempt != NULL && attempt->attempt_id != 0) {
		ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_AWAITING_MANIFEST;
		ota_state.current_attempt.identity.id = attempt->attempt_id;
		ota_state.current_attempt.identity.generation = allocate_attempt_generation();
		ota_state.current_attempt.execution.cancellation_requested =
			attempt->actionable_cancellation;
		ota_state.current_attempt.failure.state =
			attempt->has_attempt_error ? ATTEMPT_FAILURE_PRESENT : ATTEMPT_FAILURE_NONE;
		ota_state.current_attempt.failure.error = attempt->attempt_error;
		if (!attempt->has_attempt_error) {
			ota_state.current_attempt.plan.source = ARTIFACT_PLAN_PERSISTED_RESULTS;
			ota_state.current_attempt.plan.count = attempt->artifact_count;
			memcpy(ota_state.current_attempt.plan.results, attempt->artifact_results,
			       sizeof(ota_state.current_attempt.plan.results));
		}
		advance_current_artifact(&ota_state.current_attempt);
		refresh_attempt_lifecycle(&ota_state.current_attempt);
	}

	if (has_probation && probation != NULL && probation->attempt_id != 0) {
		if (!attempt_exists(&ota_state.current_attempt) ||
		    ota_state.current_attempt.identity.id != probation->attempt_id) {
			clear_attempt(&ota_state.current_attempt);
			ota_state.current_attempt.identity.lifecycle =
				ATTEMPT_LIFECYCLE_AWAITING_MANIFEST;
			ota_state.current_attempt.identity.id = probation->attempt_id;
			ota_state.current_attempt.identity.generation =
				allocate_attempt_generation();

			if (has_attempt && attempt != NULL &&
			    attempt->attempt_id == probation->attempt_id) {
				ota_state.current_attempt.plan.source =
					ARTIFACT_PLAN_PERSISTED_RESULTS;
				ota_state.current_attempt.plan.count = attempt->artifact_count;
				memcpy(ota_state.current_attempt.plan.results,
				       attempt->artifact_results,
				       sizeof(ota_state.current_attempt.plan.results));
			} else {
				ota_state.current_attempt.plan.source =
					ARTIFACT_PLAN_PROBATION_PREFIX;
				ota_state.current_attempt.plan.count =
					probation->artifact_index + 1;
				for (size_t i = 0; i < ota_state.current_attempt.plan.count; i++) {
					ota_state.current_attempt.plan.results[i] =
						SPOTFLOW_OTA_RESULT_PENDING;
				}
			}

			advance_current_artifact(&ota_state.current_attempt);
		}

		restore_main_firmware_artifact_from_probation(probation);

		if (probation->artifact_index < ota_state.current_attempt.plan.count &&
		    ota_state.current_attempt.plan.results[probation->artifact_index] ==
			    SPOTFLOW_OTA_RESULT_PENDING) {
			ota_state.current_attempt.main_firmware.probation.state =
				MAIN_PROBATION_PENDING;
		}

		refresh_attempt_lifecycle(&ota_state.current_attempt);
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_update(const struct spotflow_ota_update_msg* msg,
				     struct spotflow_ota_update_result* result)
{
	if (result == NULL) {
		LOG_ERR("result cannot be NULL");
		return -EINVAL;
	}

	memset(result, 0, sizeof(*result));

	int rc = validate_update_msg(msg);
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	assert_state_locked();

	if (!attempt_exists(&ota_state.current_attempt)) {
		start_attempt(msg, &ota_state.current_attempt);
		clear_pending_attempt();
		result->disposition = SPOTFLOW_OTA_UPDATE_STARTED;
		result->effects = SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
		result->current_attempt_id = msg->attempt_id;
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (ota_state.current_attempt.identity.id == msg->attempt_id) {
		if (ota_state.current_attempt.plan.source != ARTIFACT_PLAN_FULL_MANIFEST &&
		    ota_state.current_attempt.failure.state == ATTEMPT_FAILURE_NONE &&
		    !attempt_has_terminal_results(&ota_state.current_attempt)) {
			int rc = rehydrate_attempt(msg, &ota_state.current_attempt, result);

			k_mutex_unlock(&state_mutex);
			return rc;
		}

		result->disposition = SPOTFLOW_OTA_UPDATE_DUPLICATE;
		result->current_attempt_id = msg->attempt_id;
		if (attempt_has_reportable_results(&ota_state.current_attempt)) {
			request_report();
			result->effects |= SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
		}
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (attempt_has_terminal_results(&ota_state.current_attempt) &&
	    !artifact_result_commit_is_pending(&ota_state.current_attempt)) {
		start_attempt(msg, &ota_state.current_attempt);
		clear_pending_attempt();
		result->disposition = SPOTFLOW_OTA_UPDATE_STARTED;
		result->effects = SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
		result->current_attempt_id = msg->attempt_id;
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	store_pending_manifest(msg);
	result->disposition = SPOTFLOW_OTA_UPDATE_QUEUED;
	result->effects = supersede_current_for_pending();
	result->current_attempt_id = ota_state.current_attempt.identity.id;
	result->pending_attempt_id = msg->attempt_id;
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_reject_update(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				     struct spotflow_ota_rejection_result* result)
{
	if (result == NULL) {
		LOG_ERR("result cannot be NULL");
		return -EINVAL;
	}

	memset(result, 0, sizeof(*result));

	if (attempt_id == 0) {
		LOG_ERR("attempt_id cannot be 0");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id == attempt_id ||
	    (attempt_has_terminal_results(&ota_state.current_attempt) &&
	     !artifact_result_commit_is_pending(&ota_state.current_attempt))) {
		apply_immediate_rejection(attempt_id, error, result);
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	store_pending_rejection(attempt_id, error);
	result->disposition = SPOTFLOW_OTA_REJECTION_QUEUED;
	result->effects = supersede_current_for_pending();
	result->current_attempt_id = ota_state.current_attempt.identity.id;
	result->pending_attempt_id = attempt_id;
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_cancel(uint64_t attempt_id, struct spotflow_ota_cancel_result* result)
{
	if (result == NULL) {
		LOG_ERR("result cannot be NULL");
		return -EINVAL;
	}

	memset(result, 0, sizeof(*result));
	result->disposition = SPOTFLOW_OTA_CANCEL_NOT_CURRENT;

	if (attempt_id == 0) {
		LOG_ERR("attempt_id cannot be 0");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (attempt_has_terminal_results(&ota_state.current_attempt) ||
	    attempt_has_succeeded_artifact(&ota_state.current_attempt) ||
	    main_upgrade_is_irreversible(&ota_state.current_attempt)) {
		result->disposition = SPOTFLOW_OTA_CANCEL_IGNORED_LATE;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	ota_state.current_attempt.execution.cancellation_requested = true;
	result->disposition = SPOTFLOW_OTA_CANCEL_ACCEPTED;
	result->effects = SPOTFLOW_OTA_STATE_EFFECT_NOTIFY_CUSTOM_FIRMWARE_CANCELED |
		SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD;
	if (!artifact_transaction_is_active(&ota_state.current_attempt)) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
	}

	if (!artifact_transaction_is_active(&ota_state.current_attempt)) {
		cancel_pending_artifacts(&ota_state.current_attempt);
		if (attempt_has_terminal_results(&ota_state.current_attempt)) {
			ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_FINALIZING;
		}
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_report_request(uint64_t attempt_id,
					     struct spotflow_ota_report_result* result)
{
	if (result == NULL) {
		LOG_ERR("result cannot be NULL");
		return -EINVAL;
	}

	memset(result, 0, sizeof(*result));
	result->disposition = SPOTFLOW_OTA_REPORT_NOT_CURRENT;

	if (attempt_id == 0) {
		LOG_ERR("attempt_id cannot be 0");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (attempt_exists(&ota_state.current_attempt) &&
	    ota_state.current_attempt.identity.id == attempt_id) {
		result->disposition = SPOTFLOW_OTA_REPORT_REQUESTED;
		result->effects = SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
		request_report();
		assert_state_locked();
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_get_worker_job(struct spotflow_ota_worker_job* job)
{
	if (job == NULL) {
		return false;
	}

	clear_worker_job(job);

	k_mutex_lock(&state_mutex, K_FOREVER);
	assert_state_locked();

	if (!attempt_exists(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (ota_state.current_attempt.identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTING) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT;
		job->attempt_id = ota_state.current_attempt.identity.id;
		job->generation = ota_state.current_attempt.identity.generation;
		job->attempt_error = ota_state.current_attempt.failure.error;
		ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_REJECTION_CLAIMED;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (ota_state.current_attempt.main_firmware.probation.state ==
	    MAIN_PROBATION_COMPLETION_QUEUED) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE;
		job->attempt_id = ota_state.current_attempt.identity.id;
		job->generation = ota_state.current_attempt.identity.generation;
		job->artifact_index = ota_state.current_attempt.main_firmware.artifact_index;
		job->reconciled_result =
			ota_state.current_attempt.main_firmware.probation.reconciled_result;
		copy_artifact_out(&job->artifact,
				  &ota_state.current_attempt.main_firmware.artifact);
		ota_state.current_attempt.main_firmware.probation.state =
			MAIN_PROBATION_COMPLETION_CLAIMED;
		ota_state.current_attempt.execution.transaction.state =
			ARTIFACT_TRANSACTION_RUNNING;
		ota_state.current_attempt.execution.transaction.artifact_index =
			job->artifact_index;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (ota_state.report_state == REPORT_STATE_REQUESTED) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT;
		job->attempt_id = ota_state.current_attempt.identity.id;
		job->generation = ota_state.current_attempt.identity.generation;
		ota_state.report_state = REPORT_STATE_CLAIMED;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (attempt_has_runnable_artifact(&ota_state.current_attempt)) {
		size_t index = ota_state.current_attempt.execution.next_index;

		job->type = SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT;
		job->attempt_id = ota_state.current_attempt.identity.id;
		job->generation = ota_state.current_attempt.identity.generation;
		job->artifact_index = index;
		copy_artifact_out(&job->artifact, &ota_state.current_attempt.plan.artifacts[index]);
		ota_state.current_attempt.execution.transaction.state =
			ARTIFACT_TRANSACTION_RUNNING;
		ota_state.current_attempt.execution.transaction.artifact_index = index;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	k_mutex_unlock(&state_mutex);
	return false;
}

int spotflow_ota_state_apply_artifact_result(size_t artifact_index, enum spotflow_ota_result result,
					     spotflow_ota_state_effects* effects)
{
	if (effects == NULL) {
		LOG_ERR("effects cannot be NULL");
		return -EINVAL;
	}

	*effects = 0;

	if (result == SPOTFLOW_OTA_RESULT_PENDING) {
		LOG_ERR("result cannot be SPOTFLOW_OTA_RESULT_PENDING");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    artifact_index >= ota_state.current_attempt.plan.count) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	if (artifact_result_commit_is_pending(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EBUSY;
	}

	if (ota_state.current_attempt.plan.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING) {
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	set_artifact_result(&ota_state.current_attempt, artifact_index, result);
	refresh_attempt_lifecycle(&ota_state.current_attempt);
	*effects = artifact_result_effects();
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_stage_artifact_result(const struct spotflow_ota_worker_job* job,
					     enum spotflow_ota_result result)
{
	if (job == NULL || job->attempt_id == 0 ||
	    (job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT &&
	     job->type != SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE) ||
	    result == SPOTFLOW_OTA_RESULT_PENDING) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->attempt_id ||
	    ota_state.current_attempt.identity.generation != job->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.current_attempt.execution.transaction.state != ARTIFACT_TRANSACTION_RUNNING ||
	    ota_state.current_attempt.execution.transaction.artifact_index != job->artifact_index ||
	    job->artifact_index >= ota_state.current_attempt.plan.count ||
	    ota_state.current_attempt.plan.results[job->artifact_index] !=
		    SPOTFLOW_OTA_RESULT_PENDING ||
	    artifact_result_commit_is_pending(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	set_artifact_result(&ota_state.current_attempt, job->artifact_index, result);
	if (job->type == SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT && job->artifact.is_main) {
		ota_state.current_attempt.main_firmware.status.phase =
			SPOTFLOW_OTA_PHASE_NOT_RUNNING;
		ota_state.current_attempt.main_firmware.status.is_paused = false;
		ota_state.current_attempt.main_firmware.status.result = result;
		ota_state.current_attempt.main_firmware.abort_requested = false;
		ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_IDLE;
	}
	ota_state.current_attempt.execution.transaction.state = ARTIFACT_TRANSACTION_RESULT_STAGED;
	ota_state.current_attempt.execution.transaction.artifact_index = job->artifact_index;
	refresh_attempt_lifecycle(&ota_state.current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_artifact_result(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL ||
	    (job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT &&
	     job->type != SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE)) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->attempt_id ||
	    ota_state.current_attempt.identity.generation != job->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.current_attempt.execution.transaction.state !=
		    ARTIFACT_TRANSACTION_RESULT_STAGED ||
	    ota_state.current_attempt.execution.transaction.artifact_index != job->artifact_index) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.execution.transaction.state = ARTIFACT_TRANSACTION_IDLE;
	refresh_attempt_lifecycle(&ota_state.current_attempt);
	request_report();
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_rejected_attempt(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->attempt_id ||
	    ota_state.current_attempt.identity.generation != job->generation ||
	    ota_state.current_attempt.identity.lifecycle != ATTEMPT_LIFECYCLE_REJECTION_CLAIMED) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_REJECTED;
	request_report();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_attempt_finalization(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->attempt_id ||
	    ota_state.current_attempt.identity.generation != job->generation ||
	    artifact_result_commit_is_pending(&ota_state.current_attempt) ||
	    !attempt_has_terminal_results(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_TERMINAL;
	request_report();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_complete_report_job(const struct spotflow_ota_worker_job* job, bool prepared)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->attempt_id ||
	    ota_state.current_attempt.identity.generation != job->generation ||
	    (ota_state.report_state != REPORT_STATE_CLAIMED &&
	     ota_state.report_state != REPORT_STATE_CLAIMED_RERUN_REQUESTED)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.report_state == REPORT_STATE_CLAIMED_RERUN_REQUESTED) {
		ota_state.report_state = REPORT_STATE_REQUESTED;
	} else {
		ota_state.report_state = prepared ? REPORT_STATE_IDLE : REPORT_STATE_BLOCKED;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_queue_main_firmware_result(
	uint64_t attempt_id, size_t artifact_index, enum spotflow_ota_result result,
	struct spotflow_ota_main_firmware_state* out_state, spotflow_ota_state_effects* effects)
{
	if (attempt_id == 0 ||
	    (result != SPOTFLOW_OTA_RESULT_SUCCEEDED && result != SPOTFLOW_OTA_RESULT_FAILED) ||
	    effects == NULL) {
		return -EINVAL;
	}

	*effects = 0;
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != attempt_id ||
	    ota_state.current_attempt.main_firmware.probation.state != MAIN_PROBATION_PENDING ||
	    ota_state.current_attempt.main_firmware.presence != MAIN_FIRMWARE_PRESENT ||
	    ota_state.current_attempt.main_firmware.artifact_index != artifact_index ||
	    artifact_index >= ota_state.current_attempt.plan.count ||
	    ota_state.current_attempt.plan.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING ||
	    artifact_transaction_is_active(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.status.result = result;
	ota_state.current_attempt.main_firmware.status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	ota_state.current_attempt.main_firmware.status.is_paused = false;
	ota_state.current_attempt.main_firmware.abort_requested = false;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_IDLE;
	ota_state.current_attempt.main_firmware.probation.reconciled_result = result;
	ota_state.current_attempt.main_firmware.probation.state = MAIN_PROBATION_COMPLETION_QUEUED;

	if (out_state != NULL) {
		*out_state = ota_state.current_attempt.main_firmware.status;
	}

	*effects = SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_fail_worker_operation(const struct spotflow_ota_operation_token* token)
{
	if (token == NULL || token->attempt_id == 0 || token->generation == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != token->attempt_id ||
	    ota_state.current_attempt.identity.generation != token->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	ota_state.current_attempt.execution.transaction.state = ARTIFACT_TRANSACTION_IDLE;
	ota_state.current_attempt.execution.sequence_policy = ARTIFACT_SEQUENCE_STOP_REMAINING;
	ota_state.current_attempt.failure.state = ATTEMPT_FAILURE_PRESENT;
	ota_state.current_attempt.failure.error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
	ota_state.report_state = REPORT_STATE_IDLE;
	ota_state.current_attempt.main_firmware.status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	ota_state.current_attempt.main_firmware.status.is_paused = false;
	ota_state.current_attempt.main_firmware.status.result = SPOTFLOW_OTA_RESULT_FAILED;
	ota_state.current_attempt.main_firmware.abort_requested = false;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_IDLE;
	ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_REJECTING;
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_promote_pending(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (ota_state.pending_attempt.kind == PENDING_ATTEMPT_NONE ||
	    !attempt_has_terminal_results(&ota_state.current_attempt) ||
	    artifact_result_commit_is_pending(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EAGAIN;
	}

	if (ota_state.pending_attempt.kind == PENDING_ATTEMPT_REJECTION) {
		start_rejected_attempt(ota_state.pending_attempt.update.attempt_id,
				       ota_state.pending_attempt.rejection_error,
				       &ota_state.current_attempt);
	} else {
		start_attempt(&ota_state.pending_attempt.update, &ota_state.current_attempt);
	}

	clear_pending_attempt();
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_update_canceled(void)
{
	bool canceled;

	k_mutex_lock(&state_mutex, K_FOREVER);
	canceled = attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.execution.cancellation_requested;
	k_mutex_unlock(&state_mutex);

	return canceled;
}

void spotflow_ota_state_get_snapshot(struct spotflow_ota_state_snapshot* snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	memset(snapshot, 0, sizeof(*snapshot));

	k_mutex_lock(&state_mutex, K_FOREVER);
	assert_state_locked();

	snapshot->has_current_attempt = attempt_exists(&ota_state.current_attempt);
	snapshot->current_attempt_id = ota_state.current_attempt.identity.id;
	snapshot->current_attempt_generation = ota_state.current_attempt.identity.generation;
	snapshot->manifest_available =
		ota_state.current_attempt.plan.source == ARTIFACT_PLAN_FULL_MANIFEST;
	snapshot->artifact_result_commit_pending =
		artifact_result_commit_is_pending(&ota_state.current_attempt);
	snapshot->artifact_count = ota_state.current_attempt.plan.count;
	snapshot->current_artifact_index = ota_state.current_attempt.execution.next_index;
	snapshot->actionable_cancellation =
		ota_state.current_attempt.execution.cancellation_requested;
	snapshot->has_pending_attempt = ota_state.pending_attempt.kind != PENDING_ATTEMPT_NONE;
	snapshot->pending_attempt_id = ota_state.pending_attempt.update.attempt_id;
	snapshot->pending_is_rejection =
		ota_state.pending_attempt.kind == PENDING_ATTEMPT_REJECTION;
	snapshot->pending_rejection_error = ota_state.pending_attempt.rejection_error;
	snapshot->has_attempt_error =
		ota_state.current_attempt.failure.state == ATTEMPT_FAILURE_PRESENT;
	snapshot->attempt_error = ota_state.current_attempt.failure.error;
	snapshot->main_firmware_state = ota_state.current_attempt.main_firmware.status;
	memcpy(snapshot->artifact_results, ota_state.current_attempt.plan.results,
	       sizeof(snapshot->artifact_results));

	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_set_main_firmware_phase(enum spotflow_ota_phase phase,
					       struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.status.phase = phase;
	ota_state.current_attempt.main_firmware.status.result = SPOTFLOW_OTA_RESULT_PENDING;

	if (out_state != NULL) {
		*out_state = ota_state.current_attempt.main_firmware.status;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_set_main_firmware_result(enum spotflow_ota_result result,
						struct spotflow_ota_main_firmware_state* out_state)
{
	if (result == SPOTFLOW_OTA_RESULT_PENDING) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.status.result = result;
	ota_state.current_attempt.main_firmware.status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	ota_state.current_attempt.main_firmware.status.is_paused = false;
	ota_state.current_attempt.main_firmware.abort_requested = false;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_IDLE;

	if (out_state != NULL) {
		*out_state = ota_state.current_attempt.main_firmware.status;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_store_main_firmware_artifact(uint64_t attempt_id, size_t artifact_index,
						    const struct spotflow_ota_artifact* artifact)
{
	if (artifact == NULL) {
		LOG_ERR("artifact cannot be NULL");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.presence = MAIN_FIRMWARE_PRESENT;
	ota_state.current_attempt.main_firmware.artifact_index = artifact_index;
	copy_artifact_out(&ota_state.current_attempt.main_firmware.artifact, artifact);
	ota_state.current_attempt.main_firmware.abort_requested = false;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_HANDLER_ACTIVE;

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_get_main_firmware_info(struct spotflow_firmware_info* info,
					      struct spotflow_download_request* request_out)
{
	if (info == NULL || request_out == NULL) {
		LOG_ERR("info and request_out cannot be NULL");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.main_firmware.presence != MAIN_FIRMWARE_PRESENT) {
		k_mutex_unlock(&state_mutex);
		return -ENOENT;
	}

	info->attempt_id = ota_state.current_attempt.identity.id;
	info->slug = ota_state.current_attempt.main_firmware.artifact.slug;
	info->is_main = ota_state.current_attempt.main_firmware.artifact.is_main;
	info->version = ota_state.current_attempt.main_firmware.artifact.version;
	request_out->url = ota_state.current_attempt.main_firmware.artifact.url;
	request_out->secret = ota_state.current_attempt.main_firmware.artifact.secret;
	info->download_request = request_out;

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_finish_main_firmware_prereboot(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.main_firmware.upgrade != MAIN_UPGRADE_COMMITTING) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.execution.transaction.state = ARTIFACT_TRANSACTION_IDLE;
	ota_state.current_attempt.main_firmware.probation.state = MAIN_PROBATION_PENDING;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_REBOOT_READY;
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_enter_main_firmware_unconfirmed(
	struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.main_firmware.probation.state != MAIN_PROBATION_PENDING) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.status.phase = SPOTFLOW_OTA_PHASE_UNCONFIRMED;
	ota_state.current_attempt.main_firmware.status.is_paused = false;
	ota_state.current_attempt.main_firmware.status.result = SPOTFLOW_OTA_RESULT_PENDING;
	ota_state.current_attempt.main_firmware.abort_requested = false;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_IDLE;

	if (out_state != NULL) {
		*out_state = ota_state.current_attempt.main_firmware.status;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_set_main_firmware_paused(bool paused,
						struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    (paused &&
	     (!main_firmware_phase_allows_pause(
		      ota_state.current_attempt.main_firmware.status.phase) ||
	      ota_state.current_attempt.main_firmware.upgrade == MAIN_UPGRADE_REBOOT_STARTED)) ||
	    (!paused && !ota_state.current_attempt.main_firmware.status.is_paused)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.status.is_paused = paused;

	if (out_state != NULL) {
		*out_state = ota_state.current_attempt.main_firmware.status;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_request_main_firmware_abort(
	struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    !main_firmware_phase_allows_abort(
		    ota_state.current_attempt.main_firmware.status.phase) ||
	    ota_state.current_attempt.main_firmware.upgrade != MAIN_UPGRADE_HANDLER_ACTIVE) {
		if (out_state != NULL) {
			*out_state = ota_state.current_attempt.main_firmware.status;
		}
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.abort_requested = true;
	ota_state.current_attempt.main_firmware.status.is_paused = false;

	if (out_state != NULL) {
		*out_state = ota_state.current_attempt.main_firmware.status;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_main_firmware_abort_requested(void)
{
	bool requested;

	k_mutex_lock(&state_mutex, K_FOREVER);
	requested = attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.main_firmware.abort_requested;
	k_mutex_unlock(&state_mutex);

	return requested;
}

int spotflow_ota_state_begin_main_firmware_upgrade_commit(void)
{
	int rc = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.main_firmware.status.phase !=
		    SPOTFLOW_OTA_PHASE_PENDING_UPGRADE ||
	    ota_state.current_attempt.main_firmware.upgrade != MAIN_UPGRADE_HANDLER_ACTIVE) {
		rc = -EINVAL;
	} else if (ota_state.current_attempt.main_firmware.abort_requested ||
		   ota_state.current_attempt.execution.cancellation_requested) {
		rc = -ECANCELED;
	} else if (ota_state.current_attempt.main_firmware.status.is_paused) {
		rc = -EAGAIN;
	} else {
		ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_COMMITTING;
	}

	k_mutex_unlock(&state_mutex);
	return rc;
}

void spotflow_ota_state_cancel_main_firmware_upgrade_commit(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	if (ota_state.current_attempt.main_firmware.upgrade == MAIN_UPGRADE_COMMITTING) {
		ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_HANDLER_ACTIVE;
	}
	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_begin_main_firmware_reboot(void)
{
	int rc = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.main_firmware.status.phase !=
		    SPOTFLOW_OTA_PHASE_PENDING_REBOOT ||
	    ota_state.current_attempt.main_firmware.upgrade != MAIN_UPGRADE_REBOOT_READY) {
		rc = -EINVAL;
	} else if (ota_state.current_attempt.main_firmware.status.is_paused) {
		rc = -EAGAIN;
	} else {
		ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_REBOOT_STARTED;
	}

	k_mutex_unlock(&state_mutex);
	return rc;
}

int spotflow_ota_state_get_main_firmware_artifact_index(size_t* artifact_index)
{
	if (artifact_index == NULL) {
		LOG_ERR("artifact_index cannot be NULL");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.main_firmware.presence != MAIN_FIRMWARE_PRESENT) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	*artifact_index = ota_state.current_attempt.main_firmware.artifact_index;
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_resolve_main_firmware_probation(
	const struct spotflow_ota_operation_token* token)
{
	if (token == NULL || token->attempt_id == 0 || token->generation == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != token->attempt_id ||
	    ota_state.current_attempt.identity.generation != token->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.current_attempt.main_firmware.probation.state !=
		    MAIN_PROBATION_COMPLETION_CLAIMED ||
	    ota_state.current_attempt.execution.transaction.state !=
		    ARTIFACT_TRANSACTION_RESULT_STAGED) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.main_firmware.probation.state = MAIN_PROBATION_NONE;
	ota_state.current_attempt.main_firmware.upgrade = MAIN_UPGRADE_IDLE;
	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

static void clear_worker_job(struct spotflow_ota_worker_job* job)
{
	memset(job, 0, sizeof(*job));
	job->type = SPOTFLOW_OTA_WORKER_JOB_NONE;
}

static void clear_attempt(struct attempt_state* attempt)
{
	memset(attempt, 0, sizeof(*attempt));
	attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_EMPTY;
	attempt->plan.source = ARTIFACT_PLAN_NONE;
	attempt->execution.transaction.state = ARTIFACT_TRANSACTION_IDLE;
	attempt->execution.sequence_policy = ARTIFACT_SEQUENCE_CONTINUE;
	attempt->failure.state = ATTEMPT_FAILURE_NONE;
	attempt->main_firmware.presence = MAIN_FIRMWARE_ABSENT;
	attempt->main_firmware.upgrade = MAIN_UPGRADE_IDLE;
	attempt->main_firmware.probation.state = MAIN_PROBATION_NONE;
	attempt->main_firmware.status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	attempt->main_firmware.status.result = SPOTFLOW_OTA_RESULT_PENDING;
}

static uint32_t allocate_attempt_generation(void)
{
	ota_state.next_attempt_generation++;
	if (ota_state.next_attempt_generation == 0) {
		ota_state.next_attempt_generation++;
	}

	return ota_state.next_attempt_generation;
}

static bool attempt_exists(const struct attempt_state* attempt)
{
	return attempt->identity.lifecycle != ATTEMPT_LIFECYCLE_EMPTY;
}

static bool artifact_transaction_is_active(const struct attempt_state* attempt)
{
	return attempt->execution.transaction.state != ARTIFACT_TRANSACTION_IDLE;
}

static bool artifact_result_commit_is_pending(const struct attempt_state* attempt)
{
	return attempt->execution.transaction.state == ARTIFACT_TRANSACTION_RESULT_STAGED;
}

static bool main_probation_is_pending(const struct attempt_state* attempt)
{
	return attempt->main_firmware.probation.state != MAIN_PROBATION_NONE;
}

static bool main_upgrade_is_irreversible(const struct attempt_state* attempt)
{
	return attempt->main_firmware.upgrade == MAIN_UPGRADE_COMMITTING ||
		attempt->main_firmware.upgrade == MAIN_UPGRADE_REBOOT_READY ||
		attempt->main_firmware.upgrade == MAIN_UPGRADE_REBOOT_STARTED;
}

static bool state_is_valid(void)
{
	if (!attempt_exists(&ota_state.current_attempt)) {
		return ota_state.current_attempt.identity.id == 0 &&
			ota_state.pending_attempt.kind == PENDING_ATTEMPT_NONE &&
			ota_state.report_state == REPORT_STATE_IDLE;
	}

	if (ota_state.current_attempt.identity.id == 0 ||
	    ota_state.current_attempt.plan.count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return false;
	}

	switch (ota_state.current_attempt.plan.source) {
	case ARTIFACT_PLAN_NONE:
		if (ota_state.current_attempt.plan.count != 0 ||
		    ota_state.current_attempt.failure.state != ATTEMPT_FAILURE_PRESENT) {
			return false;
		}
		break;
	case ARTIFACT_PLAN_PROBATION_PREFIX:
	case ARTIFACT_PLAN_PERSISTED_RESULTS:
	case ARTIFACT_PLAN_FULL_MANIFEST:
		if (ota_state.current_attempt.plan.count == 0) {
			return false;
		}
		break;
	default:
		return false;
	}

	if (artifact_transaction_is_active(&ota_state.current_attempt) &&
	    ota_state.current_attempt.execution.transaction.artifact_index >=
		    ota_state.current_attempt.plan.count) {
		return false;
	}

	if (artifact_result_commit_is_pending(&ota_state.current_attempt) &&
	    ota_state.current_attempt.plan.results[ota_state.current_attempt.execution.transaction
							   .artifact_index] ==
		    SPOTFLOW_OTA_RESULT_PENDING) {
		return false;
	}

	if (main_probation_is_pending(&ota_state.current_attempt) &&
	    (ota_state.current_attempt.main_firmware.presence != MAIN_FIRMWARE_PRESENT ||
	     ota_state.current_attempt.main_firmware.artifact_index >=
		     ota_state.current_attempt.plan.count)) {
		return false;
	}

	if ((ota_state.current_attempt.identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTING ||
	     ota_state.current_attempt.identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTION_CLAIMED ||
	     ota_state.current_attempt.identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTED) &&
	    ota_state.current_attempt.failure.state != ATTEMPT_FAILURE_PRESENT) {
		return false;
	}

	return ota_state.pending_attempt.kind == PENDING_ATTEMPT_NONE ||
		ota_state.pending_attempt.update.attempt_id != 0;
}

static void assert_state_locked(void)
{
	__ASSERT_NO_MSG(state_is_valid());
}

static void request_report(void)
{
	switch (ota_state.report_state) {
	case REPORT_STATE_CLAIMED:
		ota_state.report_state = REPORT_STATE_CLAIMED_RERUN_REQUESTED;
		break;
	case REPORT_STATE_CLAIMED_RERUN_REQUESTED:
	case REPORT_STATE_REQUESTED:
		break;
	case REPORT_STATE_IDLE:
	case REPORT_STATE_BLOCKED:
	default:
		ota_state.report_state = REPORT_STATE_REQUESTED;
		break;
	}
}

static void refresh_attempt_lifecycle(struct attempt_state* attempt)
{
	if (!attempt_exists(attempt) ||
	    attempt->identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTING ||
	    attempt->identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTION_CLAIMED ||
	    attempt->identity.lifecycle == ATTEMPT_LIFECYCLE_REJECTED) {
		return;
	}

	if (attempt->failure.state == ATTEMPT_FAILURE_PRESENT) {
		attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_REJECTED;
	} else if (artifact_result_commit_is_pending(attempt)) {
		attempt->identity.lifecycle = attempt_has_terminal_results(attempt)
			? ATTEMPT_LIFECYCLE_FINALIZING
			: ATTEMPT_LIFECYCLE_ACTIVE;
	} else if (attempt_has_terminal_results(attempt)) {
		attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_TERMINAL;
	} else if (attempt->plan.source == ARTIFACT_PLAN_FULL_MANIFEST) {
		attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_ACTIVE;
	} else {
		attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_AWAITING_MANIFEST;
	}
}

static int validate_update_msg(const struct spotflow_ota_update_msg* msg)
{
	if (msg == NULL || msg->attempt_id == 0 || msg->artifact_count == 0 ||
	    msg->artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	for (size_t i = 0; i < msg->artifact_count; i++) {
		if (!validate_artifact(&msg->artifacts[i])) {
			return -EINVAL;
		}
	}

	return 0;
}

static bool validate_artifact(const struct spotflow_ota_artifact* artifact)
{
	if (artifact == NULL || artifact->slug[0] == '\0' || artifact->version[0] == '\0' ||
	    artifact->url[0] == '\0' || artifact->secret[0] == '\0' ||
	    memchr(artifact->slug, '\0', sizeof(artifact->slug)) == NULL ||
	    memchr(artifact->version, '\0', sizeof(artifact->version)) == NULL ||
	    memchr(artifact->url, '\0', sizeof(artifact->url)) == NULL ||
	    memchr(artifact->secret, '\0', sizeof(artifact->secret)) == NULL) {
		return false;
	}

	return strchr(artifact->slug, '/') == NULL;
}

static void start_attempt(const struct spotflow_ota_update_msg* msg, struct attempt_state* attempt)
{
	clear_attempt(attempt);
	attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_ACTIVE;
	attempt->identity.id = msg->attempt_id;
	attempt->identity.generation = allocate_attempt_generation();
	attempt->plan.source = ARTIFACT_PLAN_FULL_MANIFEST;
	attempt->plan.count = msg->artifact_count;
	memcpy(attempt->plan.artifacts, msg->artifacts,
	       msg->artifact_count * sizeof(attempt->plan.artifacts[0]));
	ota_state.report_state = REPORT_STATE_IDLE;

	for (size_t i = 0; i < msg->artifact_count; i++) {
		attempt->plan.results[i] = msg->is_canceled ? SPOTFLOW_OTA_RESULT_CANCELED
							    : SPOTFLOW_OTA_RESULT_PENDING;
	}

	attempt->execution.cancellation_requested = msg->is_canceled;
	advance_current_artifact(attempt);
	refresh_attempt_lifecycle(attempt);
	if (msg->is_canceled) {
		attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_FINALIZING;
	}
}

static int rehydrate_attempt(const struct spotflow_ota_update_msg* msg,
			     struct attempt_state* attempt,
			     struct spotflow_ota_update_result* result)
{
	if ((attempt->plan.source == ARTIFACT_PLAN_PERSISTED_RESULTS ||
	     attempt->plan.source == ARTIFACT_PLAN_FULL_MANIFEST) &&
	    attempt->plan.count != msg->artifact_count) {
		LOG_ERR("Cannot rehydrate OTA attempt %llu: persisted artifact count %zu does not "
			"match received count %zu",
			(unsigned long long)attempt->identity.id, attempt->plan.count,
			msg->artifact_count);
		return -EINVAL;
	}

	if (attempt->plan.source == ARTIFACT_PLAN_PROBATION_PREFIX &&
	    attempt->plan.count > msg->artifact_count) {
		LOG_ERR("Cannot rehydrate OTA attempt %llu: probation requires at least %zu "
			"artifacts, but received %zu",
			(unsigned long long)attempt->identity.id, attempt->plan.count,
			msg->artifact_count);
		return -EINVAL;
	}

	size_t previous_artifact_count = attempt->plan.count;
	attempt->plan.source = ARTIFACT_PLAN_FULL_MANIFEST;
	attempt->plan.count = msg->artifact_count;
	memcpy(attempt->plan.artifacts, msg->artifacts,
	       msg->artifact_count * sizeof(attempt->plan.artifacts[0]));
	for (size_t i = previous_artifact_count; i < attempt->plan.count; i++) {
		attempt->plan.results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}
	advance_current_artifact(attempt);

	result->disposition = SPOTFLOW_OTA_UPDATE_REHYDRATED;
	result->current_attempt_id = msg->attempt_id;
	if (attempt_has_reportable_results(attempt)) {
		request_report();
	}

	if (msg->is_canceled && !attempt_has_succeeded_artifact(attempt)) {
		attempt->execution.cancellation_requested = true;
		cancel_pending_artifacts(attempt);
	}

	if (attempt_has_terminal_results(attempt) || attempt_has_runnable_artifact(attempt)) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
	}
	refresh_attempt_lifecycle(attempt);
	assert_state_locked();
	return 0;
}

static void start_rejected_attempt(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				   struct attempt_state* attempt)
{
	clear_attempt(attempt);
	attempt->identity.lifecycle = ATTEMPT_LIFECYCLE_REJECTING;
	attempt->identity.id = attempt_id;
	attempt->identity.generation = allocate_attempt_generation();
	attempt->failure.state = ATTEMPT_FAILURE_PRESENT;
	attempt->failure.error = error;
	ota_state.report_state = REPORT_STATE_IDLE;
}

static void clear_pending_attempt(void)
{
	memset(&ota_state.pending_attempt, 0, sizeof(ota_state.pending_attempt));
	ota_state.pending_attempt.kind = PENDING_ATTEMPT_NONE;
	ota_state.pending_attempt.rejection_error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
}

static void store_pending_manifest(const struct spotflow_ota_update_msg* msg)
{
	ota_state.pending_attempt.kind = PENDING_ATTEMPT_UPDATE;
	ota_state.pending_attempt.update = *msg;
	ota_state.pending_attempt.rejection_error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
}

static void store_pending_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error)
{
	memset(&ota_state.pending_attempt.update, 0, sizeof(ota_state.pending_attempt.update));
	ota_state.pending_attempt.kind = PENDING_ATTEMPT_REJECTION;
	ota_state.pending_attempt.update.attempt_id = attempt_id;
	ota_state.pending_attempt.rejection_error = error;
}

static void apply_immediate_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				      struct spotflow_ota_rejection_result* result)
{
	start_rejected_attempt(attempt_id, error, &ota_state.current_attempt);
	clear_pending_attempt();
	result->disposition = SPOTFLOW_OTA_REJECTION_STARTED;
	result->effects = SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
	result->current_attempt_id = attempt_id;
}

static spotflow_ota_state_effects supersede_current_for_pending(void)
{
	spotflow_ota_state_effects effects =
		SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD;

	if (main_upgrade_is_irreversible(&ota_state.current_attempt)) {
		return effects;
	}

	ota_state.current_attempt.execution.cancellation_requested = true;
	cancel_pending_artifacts(&ota_state.current_attempt);
	if (attempt_has_terminal_results(&ota_state.current_attempt)) {
		ota_state.current_attempt.identity.lifecycle = ATTEMPT_LIFECYCLE_FINALIZING;
	}
	return effects | SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
}

static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source)
{
	*destination = *source;
}

static bool attempt_has_terminal_results(const struct attempt_state* attempt)
{
	if (!attempt_exists(attempt)) {
		return true;
	}

	if (attempt->failure.state == ATTEMPT_FAILURE_PRESENT) {
		return true;
	}

	if (attempt->plan.count == 0) {
		return false;
	}

	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			return false;
		}
	}

	return true;
}

static bool attempt_has_reportable_results(const struct attempt_state* attempt)
{
	if (!attempt_exists(attempt)) {
		return false;
	}

	if (attempt->failure.state == ATTEMPT_FAILURE_PRESENT) {
		return true;
	}

	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] != SPOTFLOW_OTA_RESULT_PENDING) {
			return true;
		}
	}

	return false;
}

static bool attempt_has_succeeded_artifact(const struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
			return true;
		}
	}

	return false;
}

static bool attempt_has_runnable_artifact(const struct attempt_state* attempt)
{
	return attempt_exists(attempt) && attempt->plan.source == ARTIFACT_PLAN_FULL_MANIFEST &&
		attempt->execution.sequence_policy == ARTIFACT_SEQUENCE_CONTINUE &&
		!attempt->execution.cancellation_requested &&
		!artifact_transaction_is_active(attempt) && !main_probation_is_pending(attempt) &&
		attempt->execution.next_index < attempt->plan.count &&
		attempt->plan.results[attempt->execution.next_index] == SPOTFLOW_OTA_RESULT_PENDING;
}

static void set_artifact_result(struct attempt_state* attempt, size_t artifact_index,
				enum spotflow_ota_result result)
{
	attempt->plan.results[artifact_index] = result;
	attempt->execution.transaction.state = ARTIFACT_TRANSACTION_IDLE;

	if (attempt->execution.cancellation_requested && result == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
		attempt->execution.cancellation_requested = false;
	}

	if (result == SPOTFLOW_OTA_RESULT_FAILED || result == SPOTFLOW_OTA_RESULT_CANCELED) {
		attempt->execution.sequence_policy = ARTIFACT_SEQUENCE_STOP_REMAINING;
		cancel_pending_artifacts(attempt);
	} else if (attempt->execution.cancellation_requested) {
		cancel_pending_artifacts(attempt);
	}

	advance_current_artifact(attempt);
}

static spotflow_ota_state_effects artifact_result_effects(void)
{
	return !attempt_has_terminal_results(&ota_state.current_attempt) &&
			attempt_has_runnable_artifact(&ota_state.current_attempt)
		? SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER
		: 0;
}

static bool main_firmware_phase_allows_pause(enum spotflow_ota_phase phase)
{
	switch (phase) {
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
	case SPOTFLOW_OTA_PHASE_PENDING_REBOOT:
		return true;
	default:
		return false;
	}
}

static bool main_firmware_phase_allows_abort(enum spotflow_ota_phase phase)
{
	switch (phase) {
	case SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD:
	case SPOTFLOW_OTA_PHASE_DOWNLOADING:
	case SPOTFLOW_OTA_PHASE_PENDING_UPGRADE:
		return true;
	default:
		return false;
	}
}

static void cancel_pending_artifacts(struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (artifact_transaction_is_active(attempt) &&
		    i == attempt->execution.transaction.artifact_index) {
			continue;
		}
		if (main_probation_is_pending(attempt) &&
		    i == attempt->main_firmware.artifact_index) {
			continue;
		}

		if (attempt->plan.results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			attempt->plan.results[i] = SPOTFLOW_OTA_RESULT_CANCELED;
		}
	}

	advance_current_artifact(attempt);
}

static void advance_current_artifact(struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			attempt->execution.next_index = i;
			return;
		}
	}

	attempt->execution.next_index = attempt->plan.count;
}

static void
restore_main_firmware_artifact_from_probation(const struct spotflow_ota_probation* probation)
{
	struct spotflow_ota_artifact artifact = {
		.is_main = true,
	};

	strncpy(artifact.slug, probation->slug, sizeof(artifact.slug) - 1);
	strncpy(artifact.version, probation->version, sizeof(artifact.version) - 1);

	ota_state.current_attempt.main_firmware.presence = MAIN_FIRMWARE_PRESENT;
	ota_state.current_attempt.main_firmware.artifact_index = probation->artifact_index;
	copy_artifact_out(&ota_state.current_attempt.main_firmware.artifact, &artifact);
}
