#include "ota/core/spotflow_ota_state.h"
#include "ota/core/spotflow_ota_attempt_model.h"
#include "ota/core/spotflow_ota_main_model.h"

#include "ota/persistence/spotflow_ota_records_cbor.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

struct ota_state_store {
	struct ota_attempt_model current_attempt;
	struct ota_main_firmware_model main_firmware;
	struct ota_pending_attempt pending_attempt;
	enum ota_report_state report_state;
	uint32_t report_claim_generation;
	uint32_t next_attempt_generation;
};

static struct ota_state_store ota_state;
static K_MUTEX_DEFINE(state_mutex);

static void clear_worker_job(struct spotflow_ota_worker_job* job);
static uint32_t allocate_attempt_generation(void);
static struct ota_attempt_constraints attempt_constraints(void);
static bool state_is_valid(void);
static void assert_state_locked(void);
static void clear_report_state(void);
static void request_report(void);
static void clear_pending_attempt(void);
static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source);
static spotflow_ota_state_effects artifact_result_effects(void);
static bool operation_token_matches_current(const struct spotflow_ota_operation_token* token);
static bool main_firmware_handler_is_owned(const struct spotflow_ota_operation_token* token);

void spotflow_ota_state_reset(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	spotflow_ota_attempt_clear(&ota_state.current_attempt);
	spotflow_ota_main_clear(&ota_state.main_firmware);
	clear_pending_attempt();
	clear_report_state();
	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_init_from_persistence(const struct spotflow_ota_persisted_attempt* attempt,
					     bool has_attempt,
					     const struct spotflow_ota_probation* probation,
					     bool has_probation)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	spotflow_ota_attempt_clear(&ota_state.current_attempt);
	spotflow_ota_main_clear(&ota_state.main_firmware);
	clear_pending_attempt();
	clear_report_state();

	if (has_attempt && attempt != NULL && attempt->attempt_id != 0) {
		ota_state.current_attempt.identity.lifecycle = OTA_ATTEMPT_AWAITING_MANIFEST;
		ota_state.current_attempt.identity.id = attempt->attempt_id;
		ota_state.current_attempt.identity.generation = allocate_attempt_generation();
		ota_state.current_attempt.execution.cancellation_requested =
			attempt->actionable_cancellation;
		ota_state.current_attempt.failure.state = attempt->has_attempt_error
			? OTA_ATTEMPT_FAILURE_PRESENT
			: OTA_ATTEMPT_FAILURE_NONE;
		ota_state.current_attempt.failure.error = attempt->attempt_error;
		if (!attempt->has_attempt_error) {
			ota_state.current_attempt.plan.source = OTA_ARTIFACT_PLAN_PERSISTED_RESULTS;
			ota_state.current_attempt.plan.count = attempt->artifact_count;
			memcpy(ota_state.current_attempt.plan.results, attempt->artifact_results,
			       sizeof(ota_state.current_attempt.plan.results));
		}
		spotflow_ota_attempt_advance(&ota_state.current_attempt);
		spotflow_ota_attempt_refresh_lifecycle(&ota_state.current_attempt,
						       attempt_constraints());
	}

	if (has_probation && probation != NULL && probation->attempt_id != 0) {
		if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
		    ota_state.current_attempt.identity.id != probation->attempt_id) {
			spotflow_ota_attempt_clear(&ota_state.current_attempt);
			ota_state.current_attempt.identity.lifecycle =
				OTA_ATTEMPT_AWAITING_MANIFEST;
			ota_state.current_attempt.identity.id = probation->attempt_id;
			ota_state.current_attempt.identity.generation =
				allocate_attempt_generation();

			if (has_attempt && attempt != NULL &&
			    attempt->attempt_id == probation->attempt_id) {
				ota_state.current_attempt.plan.source =
					OTA_ARTIFACT_PLAN_PERSISTED_RESULTS;
				ota_state.current_attempt.plan.count = attempt->artifact_count;
				memcpy(ota_state.current_attempt.plan.results,
				       attempt->artifact_results,
				       sizeof(ota_state.current_attempt.plan.results));
			} else {
				ota_state.current_attempt.plan.source =
					OTA_ARTIFACT_PLAN_PROBATION_PREFIX;
				ota_state.current_attempt.plan.count =
					probation->artifact_index + 1;
				for (size_t i = 0; i < ota_state.current_attempt.plan.count; i++) {
					ota_state.current_attempt.plan.results[i] =
						SPOTFLOW_OTA_RESULT_PENDING;
				}
			}

			spotflow_ota_attempt_advance(&ota_state.current_attempt);
		}

		bool result_pending =
			probation->artifact_index < ota_state.current_attempt.plan.count &&
			ota_state.current_attempt.plan.results[probation->artifact_index] ==
				SPOTFLOW_OTA_RESULT_PENDING;
		spotflow_ota_main_restore_probation(&ota_state.main_firmware, probation,
						    result_pending);

		spotflow_ota_attempt_refresh_lifecycle(&ota_state.current_attempt,
						       attempt_constraints());
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

	int rc = spotflow_ota_attempt_validate_update(msg);
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	assert_state_locked();

	struct ota_attempt_update_transition transition;
	rc = spotflow_ota_attempt_accept_update(
		&ota_state.current_attempt, &ota_state.pending_attempt,
		&ota_state.next_attempt_generation, msg, attempt_constraints(), &transition);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}
	switch (transition.outcome) {
	case OTA_ATTEMPT_UPDATE_STARTED:
		result->disposition = SPOTFLOW_OTA_UPDATE_STARTED;
		spotflow_ota_main_clear(&ota_state.main_firmware);
		clear_report_state();
		break;
	case OTA_ATTEMPT_UPDATE_REHYDRATED:
		result->disposition = SPOTFLOW_OTA_UPDATE_REHYDRATED;
		break;
	case OTA_ATTEMPT_UPDATE_DUPLICATE:
		result->disposition = SPOTFLOW_OTA_UPDATE_DUPLICATE;
		break;
	case OTA_ATTEMPT_UPDATE_QUEUED:
		result->disposition = SPOTFLOW_OTA_UPDATE_QUEUED;
		break;
	}
	if (transition.request_report) {
		request_report();
	}
	if (transition.wake_worker) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
	}
	if (transition.cancel_main_firmware) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD;
	}
	result->current_attempt_id = transition.current_attempt_id;
	result->pending_attempt_id = transition.pending_attempt_id;
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

	struct ota_attempt_rejection_transition transition;
	spotflow_ota_attempt_reject_update(&ota_state.current_attempt, &ota_state.pending_attempt,
					   &ota_state.next_attempt_generation, attempt_id, error,
					   attempt_constraints(), &transition);
	if (transition.outcome == OTA_ATTEMPT_REJECTION_STARTED) {
		result->disposition = SPOTFLOW_OTA_REJECTION_STARTED;
		spotflow_ota_main_clear(&ota_state.main_firmware);
		clear_report_state();
	} else {
		result->disposition = SPOTFLOW_OTA_REJECTION_QUEUED;
	}
	if (transition.wake_worker) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
	}
	if (transition.cancel_main_firmware) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD;
	}
	result->current_attempt_id = transition.current_attempt_id;
	result->pending_attempt_id = transition.pending_attempt_id;
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

	struct ota_attempt_cancel_transition transition = spotflow_ota_attempt_accept_cancel(
		&ota_state.current_attempt, attempt_id, attempt_constraints());
	if (transition.outcome == OTA_ATTEMPT_CANCEL_NOT_CURRENT) {
		k_mutex_unlock(&state_mutex);
		return 0;
	}
	if (transition.outcome == OTA_ATTEMPT_CANCEL_IGNORED_LATE) {
		result->disposition = SPOTFLOW_OTA_CANCEL_IGNORED_LATE;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	result->disposition = SPOTFLOW_OTA_CANCEL_ACCEPTED;
	result->effects = SPOTFLOW_OTA_STATE_EFFECT_NOTIFY_CUSTOM_FIRMWARE_CANCELED |
		SPOTFLOW_OTA_STATE_EFFECT_CANCEL_MAIN_FIRMWARE_DOWNLOAD;
	if (transition.wake_worker) {
		result->effects |= SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER;
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

	if (spotflow_ota_attempt_exists(&ota_state.current_attempt) &&
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

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (ota_state.current_attempt.identity.lifecycle == OTA_ATTEMPT_REJECTING) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT;
		job->token.attempt_id = ota_state.current_attempt.identity.id;
		job->token.generation = ota_state.current_attempt.identity.generation;
		job->data.rejected_attempt.error = ota_state.current_attempt.failure.error;
		ota_state.current_attempt.identity.lifecycle = OTA_ATTEMPT_REJECTION_CLAIMED;
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return true;
	}

	struct ota_main_completion_transition completion;
	if (spotflow_ota_main_claim_completion(&ota_state.main_firmware, &completion)) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE;
		job->token.attempt_id = ota_state.current_attempt.identity.id;
		job->token.generation = ota_state.current_attempt.identity.generation;
		job->data.complete_main_firmware.artifact_index = completion.artifact_index;
		job->data.complete_main_firmware.result = completion.result;
		job->data.complete_main_firmware.artifact = completion.artifact;
		if (completion.claim_artifact_transaction) {
			ota_state.current_attempt.execution.transaction.state =
				OTA_ARTIFACT_TRANSACTION_RUNNING;
			ota_state.current_attempt.execution.transaction.artifact_index =
				completion.artifact_index;
		}
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (spotflow_ota_attempt_transaction_is_active(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (ota_state.current_attempt.identity.lifecycle == OTA_ATTEMPT_FINALIZING &&
	    !spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt)) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_FINALIZE_ATTEMPT;
		job->token.attempt_id = ota_state.current_attempt.identity.id;
		job->token.generation = ota_state.current_attempt.identity.generation;
		ota_state.current_attempt.identity.lifecycle = OTA_ATTEMPT_FINALIZATION_CLAIMED;
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (ota_state.current_attempt.identity.lifecycle == OTA_ATTEMPT_FINALIZATION_CLAIMED) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (ota_state.report_state == OTA_REPORT_REQUESTED) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT;
		job->token.attempt_id = ota_state.current_attempt.identity.id;
		job->token.generation = ota_state.current_attempt.identity.generation;
		ota_state.report_state = OTA_REPORT_CLAIMED;
		ota_state.report_claim_generation = ota_state.current_attempt.identity.generation;
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (ota_state.report_state == OTA_REPORT_CLAIMED ||
	    ota_state.report_state == OTA_REPORT_CLAIMED_RERUN_REQUESTED) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (spotflow_ota_attempt_has_runnable_artifact(&ota_state.current_attempt,
						       attempt_constraints())) {
		size_t index = ota_state.current_attempt.execution.next_index;

		job->type = SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT;
		job->token.attempt_id = ota_state.current_attempt.identity.id;
		job->token.generation = ota_state.current_attempt.identity.generation;
		job->data.process_artifact.artifact_index = index;
		copy_artifact_out(&job->data.process_artifact.artifact,
				  &ota_state.current_attempt.plan.artifacts[index]);
		ota_state.current_attempt.execution.transaction.state =
			OTA_ARTIFACT_TRANSACTION_RUNNING;
		ota_state.current_attempt.execution.transaction.artifact_index = index;
		assert_state_locked();
		k_mutex_unlock(&state_mutex);
		return true;
	}

	k_mutex_unlock(&state_mutex);
	return false;
}

int spotflow_ota_state_capture_persisted_attempt(const struct spotflow_ota_worker_job* job,
						 enum spotflow_ota_persistence_view view,
						 struct spotflow_ota_persistence_capture* capture)
{
	if (job == NULL || capture == NULL || job->token.attempt_id == 0 ||
	    job->token.generation == 0 ||
	    (view != SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE &&
	     view != SPOTFLOW_OTA_PERSISTENCE_VIEW_STAGED_RESULT)) {
		return -EINVAL;
	}

	memset(capture, 0, sizeof(*capture));
	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->token.attempt_id) {
		k_mutex_unlock(&state_mutex);
		return -ENOENT;
	}

	if (ota_state.current_attempt.identity.generation != job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	bool valid = false;
	switch (job->type) {
	case SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT:
		valid = ota_state.current_attempt.execution.transaction.artifact_index ==
				job->data.process_artifact.artifact_index &&
			((view == SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE &&
			  ota_state.current_attempt.execution.transaction.state ==
				  OTA_ARTIFACT_TRANSACTION_RUNNING) ||
			 (view == SPOTFLOW_OTA_PERSISTENCE_VIEW_STAGED_RESULT &&
			  spotflow_ota_attempt_result_commit_is_pending(
				  &ota_state.current_attempt) &&
			  ota_state.current_attempt.execution.transaction.mutation.source ==
				  OTA_ARTIFACT_RESULT_HANDLER));
		break;
	case SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE:
		valid = view == SPOTFLOW_OTA_PERSISTENCE_VIEW_STAGED_RESULT &&
			spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt) &&
			ota_state.current_attempt.execution.transaction.artifact_index ==
				job->data.complete_main_firmware.artifact_index &&
			ota_state.current_attempt.execution.transaction.mutation.source ==
				OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION;
		break;
	case SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT:
		valid = view == SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE &&
			ota_state.current_attempt.identity.lifecycle ==
				OTA_ATTEMPT_REJECTION_CLAIMED &&
			ota_state.current_attempt.failure.state == OTA_ATTEMPT_FAILURE_PRESENT;
		break;
	case SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT:
		valid = view == SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE &&
			!spotflow_ota_attempt_result_commit_is_pending(
				&ota_state.current_attempt) &&
			ota_state.report_claim_generation == job->token.generation &&
			(ota_state.report_state == OTA_REPORT_CLAIMED ||
			 ota_state.report_state == OTA_REPORT_CLAIMED_RERUN_REQUESTED);
		break;
	case SPOTFLOW_OTA_WORKER_JOB_FINALIZE_ATTEMPT:
		valid = view == SPOTFLOW_OTA_PERSISTENCE_VIEW_DURABLE &&
			ota_state.current_attempt.identity.lifecycle ==
				OTA_ATTEMPT_FINALIZATION_CLAIMED &&
			!spotflow_ota_attempt_result_commit_is_pending(
				&ota_state.current_attempt) &&
			spotflow_ota_attempt_has_terminal_results(&ota_state.current_attempt);
		break;
	case SPOTFLOW_OTA_WORKER_JOB_NONE:
	default:
		break;
	}

	if (!valid) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	capture->attempt = (struct spotflow_ota_persisted_attempt){
		.attempt_id = ota_state.current_attempt.identity.id,
		.artifact_count = ota_state.current_attempt.plan.count,
		.actionable_cancellation =
			ota_state.current_attempt.execution.cancellation_requested,
		.has_attempt_error =
			ota_state.current_attempt.failure.state == OTA_ATTEMPT_FAILURE_PRESENT,
		.attempt_error = ota_state.current_attempt.failure.error,
	};
	if (view == SPOTFLOW_OTA_PERSISTENCE_VIEW_STAGED_RESULT) {
		spotflow_ota_attempt_project_results(&ota_state.current_attempt,
						     attempt_constraints(),
						     capture->attempt.artifact_results);
		capture->mutation_revision =
			ota_state.current_attempt.execution.transaction.mutation_revision;
	} else {
		memcpy(capture->attempt.artifact_results, ota_state.current_attempt.plan.results,
		       sizeof(capture->attempt.artifact_results));
	}

	k_mutex_unlock(&state_mutex);
	return 0;
}

#if defined(CONFIG_ZTEST)
int spotflow_ota_state_test_apply_artifact_result(size_t artifact_index,
						  enum spotflow_ota_result result,
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

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    artifact_index >= ota_state.current_attempt.plan.count) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	if (spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EBUSY;
	}

	if (ota_state.current_attempt.plan.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING) {
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	spotflow_ota_attempt_stage_result(&ota_state.current_attempt, artifact_index, result,
					  OTA_ARTIFACT_RESULT_HANDLER);
	spotflow_ota_attempt_apply_staged_result(&ota_state.current_attempt, attempt_constraints());
	spotflow_ota_attempt_refresh_lifecycle(&ota_state.current_attempt, attempt_constraints());
	*effects = artifact_result_effects();
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}
#endif /* CONFIG_ZTEST */

int spotflow_ota_state_stage_artifact_result(const struct spotflow_ota_worker_job* job,
					     enum spotflow_ota_result result)
{
	if (job == NULL || job->token.attempt_id == 0 ||
	    (job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT &&
	     job->type != SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE) ||
	    result == SPOTFLOW_OTA_RESULT_PENDING) {
		return -EINVAL;
	}

	size_t artifact_index = job->type == SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT
		? job->data.process_artifact.artifact_index
		: job->data.complete_main_firmware.artifact_index;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->token.attempt_id ||
	    ota_state.current_attempt.identity.generation != job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.current_attempt.execution.transaction.state !=
		    OTA_ARTIFACT_TRANSACTION_RUNNING ||
	    ota_state.current_attempt.execution.transaction.artifact_index != artifact_index ||
	    artifact_index >= ota_state.current_attempt.plan.count ||
	    ota_state.current_attempt.plan.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING ||
	    spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	enum ota_artifact_result_source source =
		job->type == SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE
		? OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION
		: OTA_ARTIFACT_RESULT_HANDLER;
	spotflow_ota_attempt_stage_result(&ota_state.current_attempt, artifact_index, result,
					  source);
	if (job->type == SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT &&
	    job->data.process_artifact.artifact.is_main) {
		spotflow_ota_main_finish_handler(&ota_state.main_firmware, result);
	}
	spotflow_ota_attempt_refresh_lifecycle(&ota_state.current_attempt, attempt_constraints());
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_artifact_result(const struct spotflow_ota_worker_job* job,
					      uint32_t mutation_revision)
{
	if (job == NULL ||
	    (job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT &&
	     job->type != SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE) ||
	    mutation_revision == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->token.attempt_id ||
	    ota_state.current_attempt.identity.generation != job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	size_t artifact_index = job->type == SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT
		? job->data.process_artifact.artifact_index
		: job->data.complete_main_firmware.artifact_index;
	enum ota_artifact_result_source expected_source =
		job->type == SPOTFLOW_OTA_WORKER_JOB_COMPLETE_MAIN_FIRMWARE
		? OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION
		: OTA_ARTIFACT_RESULT_HANDLER;

	if (ota_state.current_attempt.execution.transaction.state !=
		    OTA_ARTIFACT_TRANSACTION_RESULT_STAGED ||
	    ota_state.current_attempt.execution.transaction.artifact_index != artifact_index ||
	    ota_state.current_attempt.execution.transaction.mutation.artifact_index !=
		    artifact_index ||
	    ota_state.current_attempt.execution.transaction.mutation.source != expected_source ||
	    (expected_source == OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION &&
	     ota_state.main_firmware.probation.state != OTA_MAIN_PROBATION_NONE)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	if (ota_state.current_attempt.execution.transaction.mutation_revision !=
	    mutation_revision) {
		k_mutex_unlock(&state_mutex);
		return -EAGAIN;
	}

	spotflow_ota_attempt_apply_staged_result(&ota_state.current_attempt, attempt_constraints());
	spotflow_ota_attempt_refresh_lifecycle(&ota_state.current_attempt, attempt_constraints());
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

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->token.attempt_id ||
	    ota_state.current_attempt.identity.generation != job->token.generation ||
	    ota_state.current_attempt.identity.lifecycle != OTA_ATTEMPT_REJECTION_CLAIMED) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	ota_state.current_attempt.identity.lifecycle = OTA_ATTEMPT_REJECTED;
	request_report();
	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_commit_attempt_finalization(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_FINALIZE_ATTEMPT) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->token.attempt_id ||
	    ota_state.current_attempt.identity.generation != job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.current_attempt.identity.lifecycle != OTA_ATTEMPT_FINALIZATION_CLAIMED ||
	    spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt) ||
	    !spotflow_ota_attempt_has_terminal_results(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	ota_state.current_attempt.identity.lifecycle = OTA_ATTEMPT_TERMINAL;
	request_report();
	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_complete_report_job(const struct spotflow_ota_worker_job* job, bool prepared)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != job->token.attempt_id ||
	    ota_state.current_attempt.identity.generation != job->token.generation ||
	    ota_state.report_claim_generation != job->token.generation ||
	    (ota_state.report_state != OTA_REPORT_CLAIMED &&
	     ota_state.report_state != OTA_REPORT_CLAIMED_RERUN_REQUESTED)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.report_state == OTA_REPORT_CLAIMED_RERUN_REQUESTED) {
		ota_state.report_state = OTA_REPORT_REQUESTED;
		ota_state.report_claim_generation = 0;
	} else {
		ota_state.report_state = prepared ? OTA_REPORT_IDLE : OTA_REPORT_BLOCKED;
		ota_state.report_claim_generation = 0;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_get_report_plan(const struct spotflow_ota_worker_job* job,
				       struct spotflow_ota_report_plan* plan)
{
	if (job == NULL || plan == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT ||
	    job->token.attempt_id == 0 || job->token.generation == 0) {
		return -EINVAL;
	}

	memset(plan, 0, sizeof(*plan));
	k_mutex_lock(&state_mutex, K_FOREVER);

	bool is_current = spotflow_ota_attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.identity.id == job->token.attempt_id;
	if (is_current && ota_state.current_attempt.identity.generation != job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (!is_current) {
		plan->continue_worker = true;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (ota_state.report_state != OTA_REPORT_CLAIMED &&
	    ota_state.report_state != OTA_REPORT_CLAIMED_RERUN_REQUESTED) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}
	if (ota_state.report_claim_generation != job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	plan->promote_pending = ota_state.pending_attempt.kind != OTA_PENDING_ATTEMPT_NONE &&
		spotflow_ota_attempt_is_durably_terminal(&ota_state.current_attempt);
	plan->continue_worker =
		!spotflow_ota_attempt_is_durably_terminal(&ota_state.current_attempt);

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

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != attempt_id ||
	    artifact_index >= ota_state.current_attempt.plan.count ||
	    ota_state.current_attempt.plan.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING ||
	    spotflow_ota_attempt_transaction_is_active(&ota_state.current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	int rc = spotflow_ota_main_queue_reconciled_result(&ota_state.main_firmware, artifact_index,
							   result, out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
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

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != token->attempt_id ||
	    ota_state.current_attempt.identity.generation != token->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	spotflow_ota_attempt_clear_transaction(&ota_state.current_attempt);
	ota_state.current_attempt.execution.sequence_policy = OTA_ARTIFACT_SEQUENCE_STOP_REMAINING;
	ota_state.current_attempt.failure.state = OTA_ATTEMPT_FAILURE_PRESENT;
	ota_state.current_attempt.failure.error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
	clear_report_state();
	spotflow_ota_main_fail_operation(&ota_state.main_firmware);
	ota_state.current_attempt.identity.lifecycle = OTA_ATTEMPT_REJECTING;
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_promote_pending(const struct spotflow_ota_worker_job* report_job)
{
	if (report_job == NULL || report_job->type != SPOTFLOW_OTA_WORKER_JOB_REPORT_ATTEMPT ||
	    report_job->token.attempt_id == 0 || report_job->token.generation == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != report_job->token.attempt_id ||
	    ota_state.current_attempt.identity.generation != report_job->token.generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	if (ota_state.pending_attempt.kind == OTA_PENDING_ATTEMPT_NONE ||
	    !spotflow_ota_attempt_is_durably_terminal(&ota_state.current_attempt) ||
	    ota_state.report_claim_generation != report_job->token.generation ||
	    (ota_state.report_state != OTA_REPORT_CLAIMED &&
	     ota_state.report_state != OTA_REPORT_CLAIMED_RERUN_REQUESTED)) {
		k_mutex_unlock(&state_mutex);
		return -EAGAIN;
	}

	uint32_t generation = allocate_attempt_generation();
	(void)spotflow_ota_attempt_promote_pending(&ota_state.current_attempt,
						   &ota_state.pending_attempt, generation);
	spotflow_ota_main_clear(&ota_state.main_firmware);
	clear_report_state();
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_update_canceled(void)
{
	bool canceled;

	k_mutex_lock(&state_mutex, K_FOREVER);
	canceled = spotflow_ota_attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.execution.cancellation_requested;
	k_mutex_unlock(&state_mutex);

	return canceled;
}

int spotflow_ota_state_validate_operation(const struct spotflow_ota_operation_token* token)
{
	if (token == NULL || token->attempt_id == 0 || token->generation == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	bool valid = spotflow_ota_attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.identity.id == token->attempt_id &&
		ota_state.current_attempt.identity.generation == token->generation &&
		ota_state.current_attempt.failure.state != OTA_ATTEMPT_FAILURE_PRESENT;
	k_mutex_unlock(&state_mutex);

	return valid ? 0 : -ESTALE;
}

int spotflow_ota_state_get_main_firmware_view(struct spotflow_ota_main_firmware_view* view)
{
	if (view == NULL) {
		return -EINVAL;
	}

	memset(view, 0, sizeof(*view));
	k_mutex_lock(&state_mutex, K_FOREVER);
	view->has_current_attempt = spotflow_ota_attempt_exists(&ota_state.current_attempt);
	view->attempt_id = ota_state.current_attempt.identity.id;
	spotflow_ota_main_project_state(&ota_state.main_firmware, &view->state);
	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_main_artifact_pending(uint64_t attempt_id, size_t artifact_index)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	bool pending = attempt_id != 0 && spotflow_ota_attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.identity.id == attempt_id &&
		artifact_index < ota_state.current_attempt.plan.count &&
		ota_state.current_attempt.plan.results[artifact_index] ==
			SPOTFLOW_OTA_RESULT_PENDING;
	k_mutex_unlock(&state_mutex);
	return pending;
}

void spotflow_ota_state_get_diagnostic(struct spotflow_ota_state_diagnostic* diagnostic)
{
	if (diagnostic == NULL) {
		return;
	}

	memset(diagnostic, 0, sizeof(*diagnostic));

	k_mutex_lock(&state_mutex, K_FOREVER);
	assert_state_locked();

	diagnostic->has_current_attempt = spotflow_ota_attempt_exists(&ota_state.current_attempt);
	diagnostic->current_attempt_id = ota_state.current_attempt.identity.id;
	diagnostic->current_attempt_generation = ota_state.current_attempt.identity.generation;
	diagnostic->current_attempt_terminal = spotflow_ota_attempt_has_projected_terminal_results(
		&ota_state.current_attempt, attempt_constraints());
	diagnostic->current_attempt_durable =
		spotflow_ota_attempt_is_durably_terminal(&ota_state.current_attempt);
	diagnostic->manifest_available =
		ota_state.current_attempt.plan.source == OTA_ARTIFACT_PLAN_FULL_MANIFEST;
	diagnostic->artifact_result_commit_pending =
		spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt);
	diagnostic->artifact_result_mutation_revision =
		ota_state.current_attempt.execution.transaction.mutation_revision;
	diagnostic->artifact_count = ota_state.current_attempt.plan.count;
	diagnostic->current_artifact_index = ota_state.current_attempt.execution.next_index;
	diagnostic->actionable_cancellation =
		ota_state.current_attempt.execution.cancellation_requested;
	diagnostic->has_pending_attempt =
		ota_state.pending_attempt.kind != OTA_PENDING_ATTEMPT_NONE;
	diagnostic->pending_attempt_id =
		spotflow_ota_pending_attempt_id(&ota_state.pending_attempt);
	diagnostic->pending_is_rejection =
		ota_state.pending_attempt.kind == OTA_PENDING_ATTEMPT_REJECTION;
	diagnostic->pending_rejection_error =
		ota_state.pending_attempt.kind == OTA_PENDING_ATTEMPT_REJECTION
		? ota_state.pending_attempt.data.rejection.error
		: SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ERROR;
	diagnostic->has_attempt_error =
		ota_state.current_attempt.failure.state == OTA_ATTEMPT_FAILURE_PRESENT;
	diagnostic->attempt_error = ota_state.current_attempt.failure.error;
	spotflow_ota_main_project_state(&ota_state.main_firmware, &diagnostic->main_firmware_state);
	memcpy(diagnostic->artifact_results, ota_state.current_attempt.plan.results,
	       sizeof(diagnostic->artifact_results));
	spotflow_ota_attempt_project_results(&ota_state.current_attempt, attempt_constraints(),
					     diagnostic->projected_artifact_results);

	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_claim_main_firmware(const struct spotflow_ota_worker_job* job,
					   struct spotflow_ota_main_firmware_state* out_state)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT ||
	    !job->data.process_artifact.artifact.is_main) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(&job->token)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	size_t artifact_index = job->data.process_artifact.artifact_index;
	if (artifact_index >= ota_state.current_attempt.plan.count ||
	    !ota_state.current_attempt.plan.artifacts[artifact_index].is_main) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	bool handler_owned = ota_state.current_attempt.execution.transaction.state ==
			OTA_ARTIFACT_TRANSACTION_RUNNING &&
		ota_state.current_attempt.execution.transaction.artifact_index == artifact_index;
	int rc = spotflow_ota_main_claim(&ota_state.main_firmware, artifact_index,
					 &ota_state.current_attempt.plan.artifacts[artifact_index],
					 handler_owned, out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_main_firmware_download_pending(
	const struct spotflow_ota_operation_token* token,
	struct spotflow_ota_main_firmware_state* out_state)
{
	if (token == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}
	int rc = spotflow_ota_main_download_pending(
		&ota_state.main_firmware, main_firmware_handler_is_owned(token), out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_main_firmware_download_started(
	const struct spotflow_ota_operation_token* token,
	struct spotflow_ota_main_firmware_state* out_state)
{
	if (token == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}
	int rc = spotflow_ota_main_download_started(
		&ota_state.main_firmware, main_firmware_handler_is_owned(token), out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_main_firmware_download_completed(
	const struct spotflow_ota_operation_token* token,
	struct spotflow_ota_main_firmware_state* out_state)
{
	if (token == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}
	int rc = spotflow_ota_main_download_completed(
		&ota_state.main_firmware, main_firmware_handler_is_owned(token), out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_fail_main_firmware(const struct spotflow_ota_operation_token* token,
					  struct spotflow_ota_main_firmware_state* out_state)
{
	if (token == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}
	int rc = spotflow_ota_main_fail_handler(&ota_state.main_firmware, out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
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

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.main_firmware.presence != OTA_MAIN_FIRMWARE_PRESENT) {
		k_mutex_unlock(&state_mutex);
		return -ENOENT;
	}

	info->attempt_id = ota_state.current_attempt.identity.id;
	info->slug = ota_state.main_firmware.artifact.slug;
	info->is_main = ota_state.main_firmware.artifact.is_main;
	info->version = ota_state.main_firmware.artifact.version;
	request_out->url = ota_state.main_firmware.artifact.url;
	request_out->secret = ota_state.main_firmware.artifact.secret;
	info->download_request = request_out;

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_finish_main_firmware_prereboot(
	const struct spotflow_ota_operation_token* token,
	struct spotflow_ota_main_firmware_state* out_state)
{
	if (token == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}
	struct ota_main_prereboot_transition transition;
	int rc = spotflow_ota_main_finish_prereboot(
		&ota_state.main_firmware, main_firmware_handler_is_owned(token), &transition);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	if (transition.clear_artifact_transaction) {
		spotflow_ota_attempt_clear_transaction(&ota_state.current_attempt);
	}
	if (out_state != NULL) {
		*out_state = transition.state;
	}
	assert_state_locked();

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_enter_main_firmware_unconfirmed(
	uint64_t attempt_id, size_t artifact_index,
	struct spotflow_ota_main_firmware_state* out_state)
{
	if (attempt_id == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	int rc = spotflow_ota_main_enter_unconfirmed(&ota_state.main_firmware, artifact_index,
						     out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_set_main_firmware_paused(bool paused,
						struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	int rc = spotflow_ota_main_set_paused(
		&ota_state.main_firmware, spotflow_ota_attempt_exists(&ota_state.current_attempt),
		paused, out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_request_main_firmware_abort(
	struct spotflow_ota_main_firmware_state* out_state)
{
	k_mutex_lock(&state_mutex, K_FOREVER);

	int rc = spotflow_ota_main_request_abort(
		&ota_state.main_firmware, spotflow_ota_attempt_exists(&ota_state.current_attempt),
		out_state);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}

	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_main_firmware_abort_requested(void)
{
	bool requested;

	k_mutex_lock(&state_mutex, K_FOREVER);
	requested = spotflow_ota_main_is_abort_requested(
		&ota_state.main_firmware, spotflow_ota_attempt_exists(&ota_state.current_attempt));
	k_mutex_unlock(&state_mutex);

	return requested;
}

int spotflow_ota_state_begin_main_firmware_upgrade_commit(
	const struct spotflow_ota_operation_token* token)
{
	if (token == NULL) {
		return -EINVAL;
	}

	int rc = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		rc = -ESTALE;
	} else {
		rc = spotflow_ota_main_begin_upgrade_commit(
			&ota_state.main_firmware, main_firmware_handler_is_owned(token),
			ota_state.current_attempt.execution.cancellation_requested);
		if (rc == 0) {
			assert_state_locked();
		}
	}

	k_mutex_unlock(&state_mutex);
	return rc;
}

int spotflow_ota_state_cancel_main_firmware_upgrade_commit(
	const struct spotflow_ota_operation_token* token)
{
	if (token == NULL) {
		return -EINVAL;
	}

	int rc = 0;
	k_mutex_lock(&state_mutex, K_FOREVER);
	if (!operation_token_matches_current(token)) {
		rc = -ESTALE;
	} else {
		rc = spotflow_ota_main_cancel_upgrade_commit(&ota_state.main_firmware);
		if (rc == 0) {
			assert_state_locked();
		}
	}
	k_mutex_unlock(&state_mutex);
	return rc;
}

int spotflow_ota_state_begin_main_firmware_reboot(const struct spotflow_ota_operation_token* token)
{
	if (token == NULL) {
		return -EINVAL;
	}

	int rc = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!operation_token_matches_current(token)) {
		rc = -ESTALE;
	} else {
		rc = spotflow_ota_main_begin_reboot(&ota_state.main_firmware);
		if (rc == 0) {
			assert_state_locked();
		}
	}

	k_mutex_unlock(&state_mutex);
	return rc;
}

int spotflow_ota_state_commit_main_firmware_probation_cleared(
	const struct spotflow_ota_operation_token* token)
{
	if (token == NULL || token->attempt_id == 0 || token->generation == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt) ||
	    ota_state.current_attempt.identity.id != token->attempt_id ||
	    ota_state.current_attempt.identity.generation != token->generation) {
		k_mutex_unlock(&state_mutex);
		return -ESTALE;
	}

	bool reconciliation_owned = ota_state.current_attempt.execution.transaction.state ==
			OTA_ARTIFACT_TRANSACTION_RESULT_STAGED &&
		ota_state.current_attempt.execution.transaction.mutation.source ==
			OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION &&
		ota_state.current_attempt.execution.transaction.mutation.artifact_index ==
			ota_state.main_firmware.artifact_index;
	int rc = spotflow_ota_main_commit_probation_cleared(&ota_state.main_firmware,
							    reconciliation_owned);
	if (rc < 0) {
		k_mutex_unlock(&state_mutex);
		return rc;
	}
	assert_state_locked();
	k_mutex_unlock(&state_mutex);
	return 0;
}

static void clear_worker_job(struct spotflow_ota_worker_job* job)
{
	memset(job, 0, sizeof(*job));
	job->type = SPOTFLOW_OTA_WORKER_JOB_NONE;
}

static uint32_t allocate_attempt_generation(void)
{
	return spotflow_ota_attempt_allocate_generation(&ota_state.next_attempt_generation);
}

static struct ota_attempt_constraints attempt_constraints(void)
{
	return (struct ota_attempt_constraints){
		.main_upgrade_irreversible =
			spotflow_ota_main_upgrade_is_irreversible(&ota_state.main_firmware),
		.probation_artifact_pending =
			spotflow_ota_main_probation_is_pending(&ota_state.main_firmware),
		.probation_artifact_index = ota_state.main_firmware.artifact_index,
	};
}

static bool state_is_valid(void)
{
	if (!spotflow_ota_attempt_exists(&ota_state.current_attempt)) {
		return spotflow_ota_attempt_is_valid(&ota_state.current_attempt,
						     attempt_constraints()) &&
			spotflow_ota_main_is_valid(&ota_state.main_firmware) &&
			ota_state.main_firmware.presence == OTA_MAIN_FIRMWARE_ABSENT &&
			ota_state.pending_attempt.kind == OTA_PENDING_ATTEMPT_NONE &&
			ota_state.report_state == OTA_REPORT_IDLE &&
			ota_state.report_claim_generation == 0;
	}

	if (!spotflow_ota_attempt_is_valid(&ota_state.current_attempt, attempt_constraints())) {
		return false;
	}
	if (ota_state.current_attempt.identity.generation != ota_state.next_attempt_generation) {
		return false;
	}
	if (!spotflow_ota_main_is_valid(&ota_state.main_firmware)) {
		return false;
	}
	if (ota_state.report_state > OTA_REPORT_BLOCKED) {
		return false;
	}

	bool report_claimed = ota_state.report_state == OTA_REPORT_CLAIMED ||
		ota_state.report_state == OTA_REPORT_CLAIMED_RERUN_REQUESTED;
	if ((report_claimed &&
	     (ota_state.report_claim_generation != ota_state.current_attempt.identity.generation ||
	      spotflow_ota_attempt_transaction_is_active(&ota_state.current_attempt))) ||
	    (!report_claimed && ota_state.report_claim_generation != 0)) {
		return false;
	}

	uint64_t pending_id = spotflow_ota_pending_attempt_id(&ota_state.pending_attempt);
	if ((ota_state.pending_attempt.kind == OTA_PENDING_ATTEMPT_NONE && pending_id != 0) ||
	    (ota_state.pending_attempt.kind != OTA_PENDING_ATTEMPT_NONE &&
	     (pending_id == 0 || pending_id == ota_state.current_attempt.identity.id))) {
		return false;
	}

	if (ota_state.main_firmware.presence == OTA_MAIN_FIRMWARE_PRESENT &&
	    ota_state.main_firmware.artifact_index >= ota_state.current_attempt.plan.count) {
		return false;
	}

	if (spotflow_ota_attempt_result_commit_is_pending(&ota_state.current_attempt)) {
		const struct ota_artifact_transaction* transaction =
			&ota_state.current_attempt.execution.transaction;

		if (transaction->mutation.source == OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION &&
		    (ota_state.main_firmware.presence != OTA_MAIN_FIRMWARE_PRESENT ||
		     ota_state.main_firmware.artifact_index != transaction->artifact_index)) {
			return false;
		}
	}

	if (spotflow_ota_main_probation_is_pending(&ota_state.main_firmware) &&
	    (ota_state.main_firmware.presence != OTA_MAIN_FIRMWARE_PRESENT ||
	     ota_state.main_firmware.artifact_index >= ota_state.current_attempt.plan.count)) {
		return false;
	}

	if (ota_state.main_firmware.probation.state == OTA_MAIN_PROBATION_COMPLETION_QUEUED) {
		if (spotflow_ota_attempt_transaction_is_active(&ota_state.current_attempt)) {
			return false;
		}
	}
	if (ota_state.main_firmware.probation.state == OTA_MAIN_PROBATION_COMPLETION_CLAIMED) {
		const struct ota_artifact_transaction* transaction =
			&ota_state.current_attempt.execution.transaction;
		if (transaction->state == OTA_ARTIFACT_TRANSACTION_IDLE ||
		    transaction->artifact_index != ota_state.main_firmware.artifact_index ||
		    (transaction->state == OTA_ARTIFACT_TRANSACTION_RESULT_STAGED &&
		     transaction->mutation.source != OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION)) {
			return false;
		}
	}

	return true;
}

static void assert_state_locked(void)
{
	__ASSERT_NO_MSG(state_is_valid());
}

static void clear_report_state(void)
{
	ota_state.report_state = OTA_REPORT_IDLE;
	ota_state.report_claim_generation = 0;
}

static void request_report(void)
{
	switch (ota_state.report_state) {
	case OTA_REPORT_CLAIMED:
		ota_state.report_state = OTA_REPORT_CLAIMED_RERUN_REQUESTED;
		break;
	case OTA_REPORT_CLAIMED_RERUN_REQUESTED:
	case OTA_REPORT_REQUESTED:
		break;
	case OTA_REPORT_IDLE:
	case OTA_REPORT_BLOCKED:
	default:
		ota_state.report_state = OTA_REPORT_REQUESTED;
		ota_state.report_claim_generation = 0;
		break;
	}
}

static void clear_pending_attempt(void)
{
	spotflow_ota_pending_attempt_clear(&ota_state.pending_attempt);
}

static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source)
{
	*destination = *source;
}

static spotflow_ota_state_effects artifact_result_effects(void)
{
	return !spotflow_ota_attempt_has_terminal_results(&ota_state.current_attempt) &&
			spotflow_ota_attempt_has_runnable_artifact(&ota_state.current_attempt,
								   attempt_constraints())
		? SPOTFLOW_OTA_STATE_EFFECT_WAKE_WORKER
		: 0;
}

static bool operation_token_matches_current(const struct spotflow_ota_operation_token* token)
{
	return token != NULL && token->attempt_id != 0 && token->generation != 0 &&
		spotflow_ota_attempt_exists(&ota_state.current_attempt) &&
		ota_state.current_attempt.identity.id == token->attempt_id &&
		ota_state.current_attempt.identity.generation == token->generation;
}

static bool main_firmware_handler_is_owned(const struct spotflow_ota_operation_token* token)
{
	return operation_token_matches_current(token) &&
		ota_state.main_firmware.presence == OTA_MAIN_FIRMWARE_PRESENT &&
		ota_state.current_attempt.execution.transaction.state ==
		OTA_ARTIFACT_TRANSACTION_RUNNING &&
		ota_state.current_attempt.execution.transaction.artifact_index ==
		ota_state.main_firmware.artifact_index;
}
