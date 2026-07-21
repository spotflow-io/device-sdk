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

enum manifest_state {
	MANIFEST_UNAVAILABLE,
	MANIFEST_AVAILABLE,
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

struct attempt_state {
	enum attempt_lifecycle lifecycle;
	uint64_t attempt_id;
	uint32_t generation;
	struct spotflow_ota_update_msg update;
	enum manifest_state manifest;
	bool artifact_count_known;
	enum spotflow_ota_result results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	size_t current_artifact_index;
	struct artifact_transaction artifact_transaction;
	bool actionable_cancellation;
	bool stop_remaining_artifacts;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	enum report_state report_state;
	enum spotflow_ota_result main_firmware_reconciled_result;
	enum main_probation_state main_probation_state;
	bool has_main_firmware_artifact;
	size_t main_firmware_artifact_index;
	struct spotflow_ota_artifact main_firmware_artifact;
	struct spotflow_ota_main_firmware_state main_firmware_state;
	bool main_firmware_abort_requested;
	enum main_upgrade_state main_upgrade_state;
};

static struct attempt_state current_attempt;
static struct pending_attempt_state pending_attempt;
static uint32_t next_attempt_generation;
static K_MUTEX_DEFINE(state_mutex);

static void clear_action(struct spotflow_ota_state_action* action);
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
static void request_report(struct attempt_state* attempt);
static void refresh_attempt_lifecycle(struct attempt_state* attempt);
static void
restore_main_firmware_artifact_from_probation(const struct spotflow_ota_probation* probation);
static int validate_update_msg(const struct spotflow_ota_update_msg* msg);
static bool validate_artifact(const struct spotflow_ota_artifact* artifact);
static void start_attempt(const struct spotflow_ota_update_msg* msg, struct attempt_state* attempt);
static int rehydrate_attempt(const struct spotflow_ota_update_msg* msg,
			     struct attempt_state* attempt,
			     struct spotflow_ota_state_action* action);
static void start_rejected_attempt(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				   struct attempt_state* attempt);
static void clear_pending_attempt(void);
static void store_pending_manifest(const struct spotflow_ota_update_msg* msg);
static void store_pending_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error);
static void apply_immediate_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				      struct spotflow_ota_state_action* action);
static void supersede_current_for_pending(struct spotflow_ota_state_action* action);
static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source);
static bool attempt_has_terminal_results(const struct attempt_state* attempt);
static bool attempt_has_reportable_results(const struct attempt_state* attempt);
static bool attempt_has_succeeded_artifact(const struct attempt_state* attempt);
static bool attempt_has_runnable_artifact(const struct attempt_state* attempt);
static void set_artifact_result(struct attempt_state* attempt, size_t artifact_index,
				enum spotflow_ota_result result);
static void fill_artifact_result_action(struct spotflow_ota_state_action* action);
static bool main_firmware_phase_allows_pause(enum spotflow_ota_phase phase);
static bool main_firmware_phase_allows_abort(enum spotflow_ota_phase phase);
static void cancel_pending_artifacts(struct attempt_state* attempt);
static void advance_current_artifact(struct attempt_state* attempt);
static void fill_action(struct spotflow_ota_state_action* action, uint64_t attempt_id);

void spotflow_ota_state_reset(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	clear_attempt(&current_attempt);
	clear_pending_attempt();
	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_init_from_persistence(const struct spotflow_ota_persisted_attempt* attempt,
					     bool has_attempt,
					     const struct spotflow_ota_probation* probation,
					     bool has_probation)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	clear_attempt(&current_attempt);
	clear_pending_attempt();

	if (has_attempt && attempt != NULL && attempt->attempt_id != 0) {
		current_attempt.lifecycle = ATTEMPT_LIFECYCLE_AWAITING_MANIFEST;
		current_attempt.attempt_id = attempt->attempt_id;
		current_attempt.generation = allocate_attempt_generation();
		current_attempt.update.artifact_count = attempt->artifact_count;
		current_attempt.artifact_count_known = true;
		current_attempt.actionable_cancellation = attempt->actionable_cancellation;
		current_attempt.has_attempt_error = attempt->has_attempt_error;
		current_attempt.attempt_error = attempt->attempt_error;
		memcpy(current_attempt.results, attempt->artifact_results,
		       sizeof(current_attempt.results));
		advance_current_artifact(&current_attempt);
		refresh_attempt_lifecycle(&current_attempt);
	}

	if (has_probation && probation != NULL && probation->attempt_id != 0) {
		if (!attempt_exists(&current_attempt) ||
		    current_attempt.attempt_id != probation->attempt_id) {
			clear_attempt(&current_attempt);
			current_attempt.lifecycle = ATTEMPT_LIFECYCLE_AWAITING_MANIFEST;
			current_attempt.attempt_id = probation->attempt_id;
			current_attempt.generation = allocate_attempt_generation();

			if (has_attempt && attempt != NULL &&
			    attempt->attempt_id == probation->attempt_id) {
				current_attempt.update.artifact_count = attempt->artifact_count;
				current_attempt.artifact_count_known = true;
				memcpy(current_attempt.results, attempt->artifact_results,
				       sizeof(current_attempt.results));
			} else {
				current_attempt.update.artifact_count =
					probation->artifact_index + 1;
				for (size_t i = 0; i < current_attempt.update.artifact_count; i++) {
					current_attempt.results[i] = SPOTFLOW_OTA_RESULT_PENDING;
				}
			}

			advance_current_artifact(&current_attempt);
		}

		restore_main_firmware_artifact_from_probation(probation);

		if (probation->artifact_index < current_attempt.update.artifact_count &&
		    current_attempt.results[probation->artifact_index] ==
			    SPOTFLOW_OTA_RESULT_PENDING) {
			current_attempt.main_probation_state = MAIN_PROBATION_PENDING;
		}

		refresh_attempt_lifecycle(&current_attempt);
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_update(const struct spotflow_ota_update_msg* msg,
				     struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		LOG_ERR("action cannot be NULL");
		return -EINVAL;
	}

	clear_action(action);

	int rc = validate_update_msg(msg);
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	assert_state_locked();

	if (!attempt_exists(&current_attempt)) {
		start_attempt(msg, &current_attempt);
		clear_pending_attempt();
		fill_action(action, msg->attempt_id);
		action->accepted_update = true;
		action->wake_worker = true;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (current_attempt.attempt_id == msg->attempt_id) {
		if (current_attempt.manifest == MANIFEST_UNAVAILABLE &&
		    !current_attempt.has_attempt_error &&
		    !attempt_has_terminal_results(&current_attempt)) {
			int rc = rehydrate_attempt(msg, &current_attempt, action);

			k_mutex_unlock(&state_mutex);
			return rc;
		}

		fill_action(action, msg->attempt_id);
		action->ignored_duplicate_update = true;
		if (attempt_has_reportable_results(&current_attempt)) {
			action->report_requested = true;
			request_report(&current_attempt);
			action->wake_worker = true;
		}
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (attempt_has_terminal_results(&current_attempt) &&
	    !artifact_result_commit_is_pending(&current_attempt)) {
		start_attempt(msg, &current_attempt);
		clear_pending_attempt();
		fill_action(action, msg->attempt_id);
		action->accepted_update = true;
		action->wake_worker = true;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	store_pending_manifest(msg);
	supersede_current_for_pending(action);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_reject_update(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				     struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		LOG_ERR("action cannot be NULL");
		return -EINVAL;
	}

	clear_action(action);

	if (attempt_id == 0) {
		LOG_ERR("attempt_id cannot be 0");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id == attempt_id ||
	    (attempt_has_terminal_results(&current_attempt) &&
	     !artifact_result_commit_is_pending(&current_attempt))) {
		apply_immediate_rejection(attempt_id, error, action);
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	store_pending_rejection(attempt_id, error);
	supersede_current_for_pending(action);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_cancel(uint64_t attempt_id, struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		LOG_ERR("action cannot be NULL");
		return -EINVAL;
	}

	clear_action(action);

	if (attempt_id == 0) {
		LOG_ERR("attempt_id cannot be 0");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	fill_action(action, attempt_id);

	if (attempt_has_terminal_results(&current_attempt) ||
	    attempt_has_succeeded_artifact(&current_attempt) ||
	    main_upgrade_is_irreversible(&current_attempt)) {
		action->ignored_late_cancel = true;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	current_attempt.actionable_cancellation = true;
	action->accepted_cancel = true;
	action->wake_worker = !artifact_transaction_is_active(&current_attempt);

	if (!artifact_transaction_is_active(&current_attempt)) {
		cancel_pending_artifacts(&current_attempt);
		if (attempt_has_terminal_results(&current_attempt)) {
			current_attempt.lifecycle = ATTEMPT_LIFECYCLE_FINALIZING;
		}
	}

	action->can_promote_pending = pending_attempt.kind != PENDING_ATTEMPT_NONE &&
		attempt_has_terminal_results(&current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_report_request(uint64_t attempt_id,
					     struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		LOG_ERR("action cannot be NULL");
		return -EINVAL;
	}

	clear_action(action);

	if (attempt_id == 0) {
		LOG_ERR("attempt_id cannot be 0");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (attempt_exists(&current_attempt) && current_attempt.attempt_id == attempt_id) {
		fill_action(action, attempt_id);
		action->report_requested = true;
		request_report(&current_attempt);
		action->wake_worker = true;
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

	if (!attempt_exists(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (current_attempt.lifecycle == ATTEMPT_LIFECYCLE_REJECTING) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT;
		job->attempt_id = current_attempt.attempt_id;
		job->generation = current_attempt.generation;
		job->attempt_error = current_attempt.attempt_error;
		current_attempt.lifecycle = ATTEMPT_LIFECYCLE_REJECTION_CLAIMED;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (current_attempt.main_probation_state == MAIN_PROBATION_COMPLETION_QUEUED) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE;
		job->attempt_id = current_attempt.attempt_id;
		job->generation = current_attempt.generation;
		job->artifact_index = current_attempt.main_firmware_artifact_index;
		job->reconciled_result = current_attempt.main_firmware_reconciled_result;
		copy_artifact_out(&job->artifact, &current_attempt.main_firmware_artifact);
		current_attempt.main_probation_state = MAIN_PROBATION_COMPLETION_CLAIMED;
		current_attempt.artifact_transaction.state = ARTIFACT_TRANSACTION_RUNNING;
		current_attempt.artifact_transaction.artifact_index = job->artifact_index;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (current_attempt.report_state == REPORT_STATE_REQUESTED) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT;
		job->attempt_id = current_attempt.attempt_id;
		job->generation = current_attempt.generation;
		current_attempt.report_state = REPORT_STATE_CLAIMED;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (attempt_has_runnable_artifact(&current_attempt)) {
		size_t index = current_attempt.current_artifact_index;

		job->type = SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT;
		job->attempt_id = current_attempt.attempt_id;
		job->generation = current_attempt.generation;
		job->artifact_index = index;
		copy_artifact_out(&job->artifact, &current_attempt.update.artifacts[index]);
		current_attempt.artifact_transaction.state = ARTIFACT_TRANSACTION_RUNNING;
		current_attempt.artifact_transaction.artifact_index = index;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	k_mutex_unlock(&state_mutex);
	return false;
}

int spotflow_ota_state_apply_artifact_result(size_t artifact_index, enum spotflow_ota_result result,
					     struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		LOG_ERR("action cannot be NULL");
		return -EINVAL;
	}

	clear_action(action);

	if (result == SPOTFLOW_OTA_RESULT_PENDING) {
		LOG_ERR("result cannot be SPOTFLOW_OTA_RESULT_PENDING");
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    artifact_index >= current_attempt.update.artifact_count) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	if (artifact_result_commit_is_pending(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EBUSY;
	}

	if (current_attempt.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING) {
		fill_action(action, current_attempt.attempt_id);
		action->can_promote_pending = pending_attempt.kind != PENDING_ATTEMPT_NONE &&
			attempt_has_terminal_results(&current_attempt);
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	set_artifact_result(&current_attempt, artifact_index, result);
	refresh_attempt_lifecycle(&current_attempt);
	fill_artifact_result_action(action);

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

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != job->attempt_id ||
	    current_attempt.generation != job->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (current_attempt.artifact_transaction.state != ARTIFACT_TRANSACTION_RUNNING ||
	    current_attempt.artifact_transaction.artifact_index != job->artifact_index ||
	    job->artifact_index >= current_attempt.update.artifact_count ||
	    current_attempt.results[job->artifact_index] != SPOTFLOW_OTA_RESULT_PENDING ||
	    artifact_result_commit_is_pending(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	set_artifact_result(&current_attempt, job->artifact_index, result);
	if (job->type == SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT && job->artifact.is_main) {
		current_attempt.main_firmware_state.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
		current_attempt.main_firmware_state.is_paused = false;
		current_attempt.main_firmware_state.result = result;
		current_attempt.main_firmware_abort_requested = false;
		current_attempt.main_upgrade_state = MAIN_UPGRADE_IDLE;
	}
	current_attempt.artifact_transaction.state = ARTIFACT_TRANSACTION_RESULT_STAGED;
	current_attempt.artifact_transaction.artifact_index = job->artifact_index;
	refresh_attempt_lifecycle(&current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_artifact_result(const struct spotflow_ota_worker_job* job,
					      struct spotflow_ota_state_action* action)
{
	if (job == NULL || action == NULL ||
	    (job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT &&
	     job->type != SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE)) {
		return -EINVAL;
	}

	clear_action(action);
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != job->attempt_id ||
	    current_attempt.generation != job->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (current_attempt.artifact_transaction.state != ARTIFACT_TRANSACTION_RESULT_STAGED ||
	    current_attempt.artifact_transaction.artifact_index != job->artifact_index) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.artifact_transaction.state = ARTIFACT_TRANSACTION_IDLE;
	refresh_attempt_lifecycle(&current_attempt);
	request_report(&current_attempt);
	fill_artifact_result_action(action);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_rejected_attempt(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != job->attempt_id ||
	    current_attempt.generation != job->generation ||
	    current_attempt.lifecycle != ATTEMPT_LIFECYCLE_REJECTION_CLAIMED) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	current_attempt.lifecycle = ATTEMPT_LIFECYCLE_REJECTED;
	request_report(&current_attempt);
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_attempt_finalization(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != job->attempt_id ||
	    current_attempt.generation != job->generation ||
	    artifact_result_commit_is_pending(&current_attempt) ||
	    !attempt_has_terminal_results(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	current_attempt.lifecycle = ATTEMPT_LIFECYCLE_TERMINAL;
	request_report(&current_attempt);
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_complete_report_job(const struct spotflow_ota_worker_job* job, bool prepared)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != job->attempt_id ||
	    current_attempt.generation != job->generation ||
	    (current_attempt.report_state != REPORT_STATE_CLAIMED &&
	     current_attempt.report_state != REPORT_STATE_CLAIMED_RERUN_REQUESTED)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (current_attempt.report_state == REPORT_STATE_CLAIMED_RERUN_REQUESTED) {
		current_attempt.report_state = REPORT_STATE_REQUESTED;
	} else {
		current_attempt.report_state = prepared ? REPORT_STATE_IDLE : REPORT_STATE_BLOCKED;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_queue_main_firmware_result(
	uint64_t attempt_id, size_t artifact_index, enum spotflow_ota_result result,
	struct spotflow_ota_main_firmware_state* out_state,
	struct spotflow_ota_state_action* action)
{
	if (attempt_id == 0 ||
	    (result != SPOTFLOW_OTA_RESULT_SUCCEEDED && result != SPOTFLOW_OTA_RESULT_FAILED) ||
	    action == NULL) {
		return -EINVAL;
	}

	clear_action(action);
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != attempt_id ||
	    current_attempt.main_probation_state != MAIN_PROBATION_PENDING ||
	    !current_attempt.has_main_firmware_artifact ||
	    current_attempt.main_firmware_artifact_index != artifact_index ||
	    artifact_index >= current_attempt.update.artifact_count ||
	    current_attempt.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING ||
	    artifact_transaction_is_active(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.main_firmware_state.result = result;
	current_attempt.main_firmware_state.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	current_attempt.main_firmware_state.is_paused = false;
	current_attempt.main_firmware_abort_requested = false;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_IDLE;
	current_attempt.main_firmware_reconciled_result = result;
	current_attempt.main_probation_state = MAIN_PROBATION_COMPLETION_QUEUED;

	if (out_state != NULL) {
		*out_state = current_attempt.main_firmware_state;
	}

	fill_action(action, attempt_id);
	action->wake_worker = true;
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_fail_worker_operation(uint64_t attempt_id,
					     struct spotflow_ota_state_action* action)
{
	if (attempt_id == 0 || action == NULL) {
		return -EINVAL;
	}

	clear_action(action);
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	current_attempt.artifact_transaction.state = ARTIFACT_TRANSACTION_IDLE;
	current_attempt.stop_remaining_artifacts = true;
	current_attempt.has_attempt_error = true;
	current_attempt.attempt_error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
	current_attempt.report_state = REPORT_STATE_IDLE;
	current_attempt.main_firmware_state.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	current_attempt.main_firmware_state.is_paused = false;
	current_attempt.main_firmware_state.result = SPOTFLOW_OTA_RESULT_FAILED;
	current_attempt.main_firmware_abort_requested = false;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_IDLE;
	current_attempt.lifecycle = ATTEMPT_LIFECYCLE_REJECTING;
	fill_action(action, attempt_id);
	action->wake_worker = true;

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_promote_pending(struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		LOG_ERR("action cannot be NULL");
		return -EINVAL;
	}

	clear_action(action);

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (pending_attempt.kind == PENDING_ATTEMPT_NONE ||
	    !attempt_has_terminal_results(&current_attempt) ||
	    artifact_result_commit_is_pending(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EAGAIN;
	}

	if (pending_attempt.kind == PENDING_ATTEMPT_REJECTION) {
		start_rejected_attempt(pending_attempt.update.attempt_id,
				       pending_attempt.rejection_error, &current_attempt);
	} else {
		start_attempt(&pending_attempt.update, &current_attempt);
	}

	clear_pending_attempt();
	fill_action(action, current_attempt.attempt_id);
	action->promoted_pending = true;
	action->wake_worker = true;

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_update_canceled(void)
{
	bool canceled;

	k_mutex_lock(&state_mutex, K_FOREVER);
	canceled = attempt_exists(&current_attempt) && current_attempt.actionable_cancellation;
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

	snapshot->has_current_attempt = attempt_exists(&current_attempt);
	snapshot->current_attempt_id = current_attempt.attempt_id;
	snapshot->current_attempt_generation = current_attempt.generation;
	snapshot->manifest_available = current_attempt.manifest == MANIFEST_AVAILABLE;
	snapshot->artifact_result_commit_pending =
		artifact_result_commit_is_pending(&current_attempt);
	snapshot->artifact_count = current_attempt.update.artifact_count;
	snapshot->current_artifact_index = current_attempt.current_artifact_index;
	snapshot->actionable_cancellation = current_attempt.actionable_cancellation;
	snapshot->has_pending_attempt = pending_attempt.kind != PENDING_ATTEMPT_NONE;
	snapshot->pending_attempt_id = pending_attempt.update.attempt_id;
	snapshot->pending_is_rejection = pending_attempt.kind == PENDING_ATTEMPT_REJECTION;
	snapshot->pending_rejection_error = pending_attempt.rejection_error;
	snapshot->has_attempt_error = current_attempt.has_attempt_error;
	snapshot->attempt_error = current_attempt.attempt_error;
	snapshot->main_firmware_state = current_attempt.main_firmware_state;
	memcpy(snapshot->artifact_results, current_attempt.results,
	       sizeof(snapshot->artifact_results));

	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_set_main_firmware_phase(enum spotflow_ota_phase phase,
					       struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.main_firmware_state.phase = phase;
	current_attempt.main_firmware_state.result = SPOTFLOW_OTA_RESULT_PENDING;

	if (out_state != NULL) {
		*out_state = current_attempt.main_firmware_state;
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

	if (!attempt_exists(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.main_firmware_state.result = result;
	current_attempt.main_firmware_state.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	current_attempt.main_firmware_state.is_paused = false;
	current_attempt.main_firmware_abort_requested = false;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_IDLE;

	if (out_state != NULL) {
		*out_state = current_attempt.main_firmware_state;
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

	if (!attempt_exists(&current_attempt) || current_attempt.attempt_id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.has_main_firmware_artifact = true;
	current_attempt.main_firmware_artifact_index = artifact_index;
	copy_artifact_out(&current_attempt.main_firmware_artifact, artifact);
	current_attempt.main_firmware_abort_requested = false;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_HANDLER_ACTIVE;

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

	if (!attempt_exists(&current_attempt) || !current_attempt.has_main_firmware_artifact) {
		k_mutex_unlock(&state_mutex);
		return -ENOENT;
	}

	info->attempt_id = current_attempt.attempt_id;
	info->slug = current_attempt.main_firmware_artifact.slug;
	info->is_main = current_attempt.main_firmware_artifact.is_main;
	info->version = current_attempt.main_firmware_artifact.version;
	request_out->url = current_attempt.main_firmware_artifact.url;
	request_out->secret = current_attempt.main_firmware_artifact.secret;
	info->download_request = request_out;

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_finish_main_firmware_prereboot(struct spotflow_ota_state_action* action)
{
	clear_action(action);

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    current_attempt.main_upgrade_state != MAIN_UPGRADE_COMMITTING) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.artifact_transaction.state = ARTIFACT_TRANSACTION_IDLE;
	current_attempt.main_probation_state = MAIN_PROBATION_PENDING;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_REBOOT_READY;
	fill_action(action, current_attempt.attempt_id);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_enter_main_firmware_unconfirmed(
	struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    current_attempt.main_probation_state != MAIN_PROBATION_PENDING) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.main_firmware_state.phase = SPOTFLOW_OTA_PHASE_UNCONFIRMED;
	current_attempt.main_firmware_state.is_paused = false;
	current_attempt.main_firmware_state.result = SPOTFLOW_OTA_RESULT_PENDING;
	current_attempt.main_firmware_abort_requested = false;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_IDLE;

	if (out_state != NULL) {
		*out_state = current_attempt.main_firmware_state;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_set_main_firmware_paused(bool paused,
						struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    (paused &&
	     (!main_firmware_phase_allows_pause(current_attempt.main_firmware_state.phase) ||
	      current_attempt.main_upgrade_state == MAIN_UPGRADE_REBOOT_STARTED)) ||
	    (!paused && !current_attempt.main_firmware_state.is_paused)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.main_firmware_state.is_paused = paused;

	if (out_state != NULL) {
		*out_state = current_attempt.main_firmware_state;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_request_main_firmware_abort(
	struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    !main_firmware_phase_allows_abort(current_attempt.main_firmware_state.phase) ||
	    current_attempt.main_upgrade_state != MAIN_UPGRADE_HANDLER_ACTIVE) {
		if (out_state != NULL) {
			*out_state = current_attempt.main_firmware_state;
		}
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	current_attempt.main_firmware_abort_requested = true;
	current_attempt.main_firmware_state.is_paused = false;

	if (out_state != NULL) {
		*out_state = current_attempt.main_firmware_state;
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_main_firmware_abort_requested(void)
{
	bool requested;

	k_mutex_lock(&state_mutex, K_FOREVER);
	requested =
		attempt_exists(&current_attempt) && current_attempt.main_firmware_abort_requested;
	k_mutex_unlock(&state_mutex);

	return requested;
}

int spotflow_ota_state_begin_main_firmware_upgrade_commit(void)
{
	int rc = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    current_attempt.main_firmware_state.phase != SPOTFLOW_OTA_PHASE_PENDING_UPGRADE ||
	    current_attempt.main_upgrade_state != MAIN_UPGRADE_HANDLER_ACTIVE) {
		rc = -EINVAL;
	} else if (current_attempt.main_firmware_abort_requested ||
		   current_attempt.actionable_cancellation) {
		rc = -ECANCELED;
	} else if (current_attempt.main_firmware_state.is_paused) {
		rc = -EAGAIN;
	} else {
		current_attempt.main_upgrade_state = MAIN_UPGRADE_COMMITTING;
	}

	k_mutex_unlock(&state_mutex);
	return rc;
}

void spotflow_ota_state_cancel_main_firmware_upgrade_commit(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	if (current_attempt.main_upgrade_state == MAIN_UPGRADE_COMMITTING) {
		current_attempt.main_upgrade_state = MAIN_UPGRADE_HANDLER_ACTIVE;
	}
	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_begin_main_firmware_reboot(void)
{
	int rc = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!attempt_exists(&current_attempt) ||
	    current_attempt.main_firmware_state.phase != SPOTFLOW_OTA_PHASE_PENDING_REBOOT ||
	    current_attempt.main_upgrade_state != MAIN_UPGRADE_REBOOT_READY) {
		rc = -EINVAL;
	} else if (current_attempt.main_firmware_state.is_paused) {
		rc = -EAGAIN;
	} else {
		current_attempt.main_upgrade_state = MAIN_UPGRADE_REBOOT_STARTED;
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

	if (!attempt_exists(&current_attempt) || !current_attempt.has_main_firmware_artifact) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	*artifact_index = current_attempt.main_firmware_artifact_index;
	k_mutex_unlock(&state_mutex);
	return 0;
}

void spotflow_ota_state_resolve_main_firmware_probation(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	current_attempt.main_probation_state = MAIN_PROBATION_NONE;
	current_attempt.main_upgrade_state = MAIN_UPGRADE_IDLE;
	k_mutex_unlock(&state_mutex);
}

static void clear_action(struct spotflow_ota_state_action* action)
{
	if (action != NULL) {
		memset(action, 0, sizeof(*action));
	}
}

static void clear_worker_job(struct spotflow_ota_worker_job* job)
{
	memset(job, 0, sizeof(*job));
	job->type = SPOTFLOW_OTA_WORKER_JOB_NONE;
}

static void clear_attempt(struct attempt_state* attempt)
{
	memset(attempt, 0, sizeof(*attempt));
	attempt->lifecycle = ATTEMPT_LIFECYCLE_EMPTY;
	attempt->manifest = MANIFEST_UNAVAILABLE;
	attempt->artifact_transaction.state = ARTIFACT_TRANSACTION_IDLE;
	attempt->report_state = REPORT_STATE_IDLE;
	attempt->main_upgrade_state = MAIN_UPGRADE_IDLE;
	attempt->main_probation_state = MAIN_PROBATION_NONE;
	attempt->main_firmware_state.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	attempt->main_firmware_state.result = SPOTFLOW_OTA_RESULT_PENDING;
}

static uint32_t allocate_attempt_generation(void)
{
	next_attempt_generation++;
	if (next_attempt_generation == 0) {
		next_attempt_generation++;
	}

	return next_attempt_generation;
}

static bool attempt_exists(const struct attempt_state* attempt)
{
	return attempt->lifecycle != ATTEMPT_LIFECYCLE_EMPTY;
}

static bool artifact_transaction_is_active(const struct attempt_state* attempt)
{
	return attempt->artifact_transaction.state != ARTIFACT_TRANSACTION_IDLE;
}

static bool artifact_result_commit_is_pending(const struct attempt_state* attempt)
{
	return attempt->artifact_transaction.state == ARTIFACT_TRANSACTION_RESULT_STAGED;
}

static bool main_probation_is_pending(const struct attempt_state* attempt)
{
	return attempt->main_probation_state != MAIN_PROBATION_NONE;
}

static bool main_upgrade_is_irreversible(const struct attempt_state* attempt)
{
	return attempt->main_upgrade_state == MAIN_UPGRADE_COMMITTING ||
		attempt->main_upgrade_state == MAIN_UPGRADE_REBOOT_READY ||
		attempt->main_upgrade_state == MAIN_UPGRADE_REBOOT_STARTED;
}

static bool state_is_valid(void)
{
	if (!attempt_exists(&current_attempt)) {
		return current_attempt.attempt_id == 0 &&
			pending_attempt.kind == PENDING_ATTEMPT_NONE;
	}

	if (current_attempt.attempt_id == 0 ||
	    current_attempt.update.artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return false;
	}

	if (artifact_transaction_is_active(&current_attempt) &&
	    current_attempt.artifact_transaction.artifact_index >=
		    current_attempt.update.artifact_count) {
		return false;
	}

	if (artifact_result_commit_is_pending(&current_attempt) &&
	    current_attempt.results[current_attempt.artifact_transaction.artifact_index] ==
		    SPOTFLOW_OTA_RESULT_PENDING) {
		return false;
	}

	if (main_probation_is_pending(&current_attempt) &&
	    (!current_attempt.has_main_firmware_artifact ||
	     current_attempt.main_firmware_artifact_index >=
		     current_attempt.update.artifact_count)) {
		return false;
	}

	if ((current_attempt.lifecycle == ATTEMPT_LIFECYCLE_REJECTING ||
	     current_attempt.lifecycle == ATTEMPT_LIFECYCLE_REJECTION_CLAIMED ||
	     current_attempt.lifecycle == ATTEMPT_LIFECYCLE_REJECTED) &&
	    !current_attempt.has_attempt_error) {
		return false;
	}

	return pending_attempt.kind == PENDING_ATTEMPT_NONE ||
		pending_attempt.update.attempt_id != 0;
}

static void assert_state_locked(void)
{
	__ASSERT_NO_MSG(state_is_valid());
}

static void request_report(struct attempt_state* attempt)
{
	switch (attempt->report_state) {
	case REPORT_STATE_CLAIMED:
		attempt->report_state = REPORT_STATE_CLAIMED_RERUN_REQUESTED;
		break;
	case REPORT_STATE_CLAIMED_RERUN_REQUESTED:
	case REPORT_STATE_REQUESTED:
		break;
	case REPORT_STATE_IDLE:
	case REPORT_STATE_BLOCKED:
	default:
		attempt->report_state = REPORT_STATE_REQUESTED;
		break;
	}
}

static void refresh_attempt_lifecycle(struct attempt_state* attempt)
{
	if (!attempt_exists(attempt) || attempt->lifecycle == ATTEMPT_LIFECYCLE_REJECTING ||
	    attempt->lifecycle == ATTEMPT_LIFECYCLE_REJECTION_CLAIMED ||
	    attempt->lifecycle == ATTEMPT_LIFECYCLE_REJECTED) {
		return;
	}

	if (attempt->has_attempt_error) {
		attempt->lifecycle = ATTEMPT_LIFECYCLE_REJECTED;
	} else if (artifact_result_commit_is_pending(attempt)) {
		attempt->lifecycle = attempt_has_terminal_results(attempt)
			? ATTEMPT_LIFECYCLE_FINALIZING
			: ATTEMPT_LIFECYCLE_ACTIVE;
	} else if (attempt_has_terminal_results(attempt)) {
		attempt->lifecycle = ATTEMPT_LIFECYCLE_TERMINAL;
	} else if (attempt->manifest == MANIFEST_AVAILABLE) {
		attempt->lifecycle = ATTEMPT_LIFECYCLE_ACTIVE;
	} else {
		attempt->lifecycle = ATTEMPT_LIFECYCLE_AWAITING_MANIFEST;
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
	attempt->lifecycle = ATTEMPT_LIFECYCLE_ACTIVE;
	attempt->attempt_id = msg->attempt_id;
	attempt->generation = allocate_attempt_generation();
	attempt->update = *msg;
	attempt->manifest = MANIFEST_AVAILABLE;
	attempt->artifact_count_known = true;

	for (size_t i = 0; i < msg->artifact_count; i++) {
		attempt->results[i] = msg->is_canceled ? SPOTFLOW_OTA_RESULT_CANCELED
						       : SPOTFLOW_OTA_RESULT_PENDING;
	}

	attempt->actionable_cancellation = msg->is_canceled;
	advance_current_artifact(attempt);
	refresh_attempt_lifecycle(attempt);
	if (msg->is_canceled) {
		attempt->lifecycle = ATTEMPT_LIFECYCLE_FINALIZING;
	}
}

static int rehydrate_attempt(const struct spotflow_ota_update_msg* msg,
			     struct attempt_state* attempt,
			     struct spotflow_ota_state_action* action)
{
	if (attempt->artifact_count_known &&
	    attempt->update.artifact_count != msg->artifact_count) {
		LOG_ERR("Cannot rehydrate OTA attempt %llu: persisted artifact count %zu does not "
			"match received count %zu",
			(unsigned long long)attempt->attempt_id, attempt->update.artifact_count,
			msg->artifact_count);
		return -EINVAL;
	}

	attempt->update = *msg;
	attempt->manifest = MANIFEST_AVAILABLE;
	attempt->artifact_count_known = true;
	advance_current_artifact(attempt);

	fill_action(action, msg->attempt_id);
	action->rehydrated_update = true;
	action->report_requested = attempt_has_reportable_results(attempt);
	if (action->report_requested) {
		request_report(attempt);
	}

	if (msg->is_canceled && !attempt_has_succeeded_artifact(attempt)) {
		attempt->actionable_cancellation = true;
		cancel_pending_artifacts(attempt);
	}

	action->wake_worker =
		attempt_has_terminal_results(attempt) || attempt_has_runnable_artifact(attempt);
	refresh_attempt_lifecycle(attempt);
	return 0;
}

static void start_rejected_attempt(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				   struct attempt_state* attempt)
{
	clear_attempt(attempt);
	attempt->lifecycle = ATTEMPT_LIFECYCLE_REJECTING;
	attempt->attempt_id = attempt_id;
	attempt->generation = allocate_attempt_generation();
	attempt->has_attempt_error = true;
	attempt->attempt_error = error;
}

static void clear_pending_attempt(void)
{
	memset(&pending_attempt, 0, sizeof(pending_attempt));
	pending_attempt.kind = PENDING_ATTEMPT_NONE;
	pending_attempt.rejection_error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
}

static void store_pending_manifest(const struct spotflow_ota_update_msg* msg)
{
	pending_attempt.kind = PENDING_ATTEMPT_UPDATE;
	pending_attempt.update = *msg;
	pending_attempt.rejection_error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
}

static void store_pending_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error)
{
	memset(&pending_attempt.update, 0, sizeof(pending_attempt.update));
	pending_attempt.kind = PENDING_ATTEMPT_REJECTION;
	pending_attempt.update.attempt_id = attempt_id;
	pending_attempt.rejection_error = error;
}

static void apply_immediate_rejection(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				      struct spotflow_ota_state_action* action)
{
	start_rejected_attempt(attempt_id, error, &current_attempt);
	clear_pending_attempt();
	fill_action(action, attempt_id);
	action->rejected_attempt = true;
	action->wake_worker = true;
}

static void supersede_current_for_pending(struct spotflow_ota_state_action* action)
{
	if (main_upgrade_is_irreversible(&current_attempt)) {
		fill_action(action, current_attempt.attempt_id);
		action->superseded_current = true;
		return;
	}

	current_attempt.actionable_cancellation = true;
	cancel_pending_artifacts(&current_attempt);
	if (attempt_has_terminal_results(&current_attempt)) {
		current_attempt.lifecycle = ATTEMPT_LIFECYCLE_FINALIZING;
	}
	fill_action(action, current_attempt.attempt_id);
	action->superseded_current = true;
	action->wake_worker = true;
	action->can_promote_pending = attempt_has_terminal_results(&current_attempt);
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

	if (attempt->has_attempt_error) {
		return true;
	}

	if (attempt->update.artifact_count == 0) {
		return false;
	}

	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (attempt->results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
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

	if (attempt->has_attempt_error) {
		return true;
	}

	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (attempt->results[i] != SPOTFLOW_OTA_RESULT_PENDING) {
			return true;
		}
	}

	return false;
}

static bool attempt_has_succeeded_artifact(const struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (attempt->results[i] == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
			return true;
		}
	}

	return false;
}

static bool attempt_has_runnable_artifact(const struct attempt_state* attempt)
{
	return attempt_exists(attempt) && attempt->manifest == MANIFEST_AVAILABLE &&
		!attempt->stop_remaining_artifacts && !attempt->actionable_cancellation &&
		!artifact_transaction_is_active(attempt) && !main_probation_is_pending(attempt) &&
		attempt->current_artifact_index < attempt->update.artifact_count &&
		attempt->results[attempt->current_artifact_index] == SPOTFLOW_OTA_RESULT_PENDING;
}

static void set_artifact_result(struct attempt_state* attempt, size_t artifact_index,
				enum spotflow_ota_result result)
{
	attempt->results[artifact_index] = result;
	attempt->artifact_transaction.state = ARTIFACT_TRANSACTION_IDLE;

	if (attempt->actionable_cancellation && result == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
		attempt->actionable_cancellation = false;
	}

	if (result == SPOTFLOW_OTA_RESULT_FAILED || result == SPOTFLOW_OTA_RESULT_CANCELED) {
		attempt->stop_remaining_artifacts = true;
		cancel_pending_artifacts(attempt);
	} else if (attempt->actionable_cancellation) {
		cancel_pending_artifacts(attempt);
	}

	advance_current_artifact(attempt);
}

static void fill_artifact_result_action(struct spotflow_ota_state_action* action)
{
	fill_action(action, current_attempt.attempt_id);
	action->wake_worker = !attempt_has_terminal_results(&current_attempt) &&
		attempt_has_runnable_artifact(&current_attempt);
	action->can_promote_pending = pending_attempt.kind != PENDING_ATTEMPT_NONE &&
		attempt_has_terminal_results(&current_attempt) &&
		!artifact_result_commit_is_pending(&current_attempt);
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
	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (artifact_transaction_is_active(attempt) &&
		    i == attempt->artifact_transaction.artifact_index) {
			continue;
		}
		if (main_probation_is_pending(attempt) &&
		    i == attempt->main_firmware_artifact_index) {
			continue;
		}

		if (attempt->results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			attempt->results[i] = SPOTFLOW_OTA_RESULT_CANCELED;
		}
	}

	advance_current_artifact(attempt);
}

static void advance_current_artifact(struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (attempt->results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			attempt->current_artifact_index = i;
			return;
		}
	}

	attempt->current_artifact_index = attempt->update.artifact_count;
}

static void fill_action(struct spotflow_ota_state_action* action, uint64_t attempt_id)
{
	assert_state_locked();
	if (action != NULL) {
		action->attempt_id = attempt_id;
	}
}

static void
restore_main_firmware_artifact_from_probation(const struct spotflow_ota_probation* probation)
{
	struct spotflow_ota_artifact artifact = {
		.is_main = true,
	};

	strncpy(artifact.slug, probation->slug, sizeof(artifact.slug) - 1);
	strncpy(artifact.version, probation->version, sizeof(artifact.version) - 1);

	current_attempt.has_main_firmware_artifact = true;
	current_attempt.main_firmware_artifact_index = probation->artifact_index;
	copy_artifact_out(&current_attempt.main_firmware_artifact, &artifact);
}
