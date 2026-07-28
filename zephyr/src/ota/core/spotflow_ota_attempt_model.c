#include "ota/core/spotflow_ota_attempt_model.h"

#include <errno.h>
#include <string.h>

static bool validate_artifact(const struct spotflow_ota_artifact* artifact);
static void cancel_pending_results(const struct ota_attempt_model* attempt,
				   struct ota_attempt_constraints constraints,
				   enum spotflow_ota_result* results);

void spotflow_ota_attempt_clear(struct ota_attempt_model* attempt)
{
	memset(attempt, 0, sizeof(*attempt));
	attempt->identity.lifecycle = OTA_ATTEMPT_EMPTY;
	attempt->plan.source = OTA_ARTIFACT_PLAN_NONE;
	attempt->execution.transaction.state = OTA_ARTIFACT_TRANSACTION_IDLE;
	attempt->execution.sequence_policy = OTA_ARTIFACT_SEQUENCE_CONTINUE;
	attempt->failure.state = OTA_ATTEMPT_FAILURE_NONE;
	attempt->main_firmware.presence = OTA_MAIN_FIRMWARE_ABSENT;
	attempt->main_firmware.upgrade = OTA_MAIN_UPGRADE_IDLE;
	attempt->main_firmware.probation.state = OTA_MAIN_PROBATION_NONE;
	attempt->main_firmware.status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	attempt->main_firmware.status.result = SPOTFLOW_OTA_RESULT_PENDING;
}

uint32_t spotflow_ota_attempt_allocate_generation(uint32_t* next_generation)
{
	(*next_generation)++;
	if (*next_generation == 0) {
		(*next_generation)++;
	}
	return *next_generation;
}

bool spotflow_ota_attempt_exists(const struct ota_attempt_model* attempt)
{
	return attempt->identity.lifecycle != OTA_ATTEMPT_EMPTY;
}

bool spotflow_ota_attempt_transaction_is_active(const struct ota_attempt_model* attempt)
{
	return attempt->execution.transaction.state != OTA_ARTIFACT_TRANSACTION_IDLE;
}

bool spotflow_ota_attempt_result_commit_is_pending(const struct ota_attempt_model* attempt)
{
	return attempt->execution.transaction.state == OTA_ARTIFACT_TRANSACTION_RESULT_STAGED;
}

bool spotflow_ota_attempt_is_durably_terminal(const struct ota_attempt_model* attempt)
{
	return attempt->identity.lifecycle == OTA_ATTEMPT_TERMINAL ||
		attempt->identity.lifecycle == OTA_ATTEMPT_REJECTED;
}

bool spotflow_ota_attempt_is_valid(const struct ota_attempt_model* attempt)
{
	if (!spotflow_ota_attempt_exists(attempt)) {
		return attempt->identity.id == 0;
	}
	if (attempt->identity.id == 0 || attempt->identity.generation == 0 ||
	    attempt->plan.count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return false;
	}

	switch (attempt->plan.source) {
	case OTA_ARTIFACT_PLAN_NONE:
		if (attempt->plan.count != 0 ||
		    attempt->failure.state != OTA_ATTEMPT_FAILURE_PRESENT) {
			return false;
		}
		break;
	case OTA_ARTIFACT_PLAN_PROBATION_PREFIX:
	case OTA_ARTIFACT_PLAN_PERSISTED_RESULTS:
	case OTA_ARTIFACT_PLAN_FULL_MANIFEST:
		if (attempt->plan.count == 0) {
			return false;
		}
		break;
	default:
		return false;
	}

	if (spotflow_ota_attempt_transaction_is_active(attempt) &&
	    attempt->execution.transaction.artifact_index >= attempt->plan.count) {
		return false;
	}
	if (spotflow_ota_attempt_result_commit_is_pending(attempt)) {
		const struct ota_artifact_transaction* transaction =
			&attempt->execution.transaction;
		if (transaction->artifact_index != transaction->mutation.artifact_index ||
		    transaction->mutation_revision == 0 ||
		    transaction->mutation.result == SPOTFLOW_OTA_RESULT_PENDING ||
		    attempt->plan.results[transaction->artifact_index] !=
			    SPOTFLOW_OTA_RESULT_PENDING ||
		    (transaction->mutation.source != OTA_ARTIFACT_RESULT_HANDLER &&
		     transaction->mutation.source != OTA_ARTIFACT_RESULT_MAIN_RECONCILIATION)) {
			return false;
		}
	}
	if ((attempt->identity.lifecycle == OTA_ATTEMPT_REJECTING ||
	     attempt->identity.lifecycle == OTA_ATTEMPT_REJECTION_CLAIMED ||
	     attempt->identity.lifecycle == OTA_ATTEMPT_REJECTED) &&
	    attempt->failure.state != OTA_ATTEMPT_FAILURE_PRESENT) {
		return false;
	}
	if (attempt->identity.lifecycle == OTA_ATTEMPT_FINALIZATION_CLAIMED &&
	    (spotflow_ota_attempt_result_commit_is_pending(attempt) ||
	     !spotflow_ota_attempt_has_terminal_results(attempt))) {
		return false;
	}
	return true;
}

int spotflow_ota_attempt_validate_update(const struct spotflow_ota_update_msg* msg)
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

int spotflow_ota_attempt_accept_update(struct ota_attempt_model* attempt,
				       struct ota_pending_attempt* pending,
				       uint32_t* next_generation,
				       const struct spotflow_ota_update_msg* msg,
				       struct ota_attempt_constraints constraints,
				       struct ota_attempt_update_transition* transition)
{
	memset(transition, 0, sizeof(*transition));
	if (!spotflow_ota_attempt_exists(attempt)) {
		spotflow_ota_attempt_start(
			msg, spotflow_ota_attempt_allocate_generation(next_generation), attempt);
		spotflow_ota_pending_attempt_clear(pending);
		transition->outcome = OTA_ATTEMPT_UPDATE_STARTED;
		transition->wake_worker = true;
		transition->current_attempt_id = msg->attempt_id;
		return 0;
	}

	if (attempt->identity.id == msg->attempt_id) {
		if (attempt->plan.source != OTA_ARTIFACT_PLAN_FULL_MANIFEST &&
		    attempt->failure.state == OTA_ATTEMPT_FAILURE_NONE &&
		    !spotflow_ota_attempt_has_terminal_results(attempt)) {
			int rc = spotflow_ota_attempt_rehydrate(msg, attempt, constraints,
								&transition->request_report,
								&transition->wake_worker);
			if (rc < 0) {
				return rc;
			}
			transition->outcome = OTA_ATTEMPT_UPDATE_REHYDRATED;
		} else {
			transition->outcome = OTA_ATTEMPT_UPDATE_DUPLICATE;
			transition->request_report =
				spotflow_ota_attempt_has_reportable_results(attempt);
			transition->wake_worker = transition->request_report;
		}
		transition->current_attempt_id = msg->attempt_id;
		return 0;
	}

	if (spotflow_ota_attempt_is_durably_terminal(attempt)) {
		spotflow_ota_attempt_start(
			msg, spotflow_ota_attempt_allocate_generation(next_generation), attempt);
		spotflow_ota_pending_attempt_clear(pending);
		transition->outcome = OTA_ATTEMPT_UPDATE_STARTED;
		transition->wake_worker = true;
		transition->current_attempt_id = msg->attempt_id;
		return 0;
	}

	spotflow_ota_pending_attempt_store_update(pending, msg);
	transition->outcome = OTA_ATTEMPT_UPDATE_QUEUED;
	transition->cancel_main_firmware = true;
	transition->wake_worker = spotflow_ota_attempt_supersede(attempt, constraints);
	transition->current_attempt_id = attempt->identity.id;
	transition->pending_attempt_id = msg->attempt_id;
	return 0;
}

void spotflow_ota_attempt_reject_update(struct ota_attempt_model* attempt,
					struct ota_pending_attempt* pending,
					uint32_t* next_generation, uint64_t attempt_id,
					enum spotflow_ota_attempt_error error,
					struct ota_attempt_constraints constraints,
					struct ota_attempt_rejection_transition* transition)
{
	memset(transition, 0, sizeof(*transition));
	if (!spotflow_ota_attempt_exists(attempt) || attempt->identity.id == attempt_id ||
	    spotflow_ota_attempt_is_durably_terminal(attempt)) {
		spotflow_ota_attempt_start_rejected(
			attempt_id, error,
			spotflow_ota_attempt_allocate_generation(next_generation), attempt);
		spotflow_ota_pending_attempt_clear(pending);
		transition->outcome = OTA_ATTEMPT_REJECTION_STARTED;
		transition->wake_worker = true;
		transition->current_attempt_id = attempt_id;
		return;
	}

	spotflow_ota_pending_attempt_store_rejection(pending, attempt_id, error);
	transition->outcome = OTA_ATTEMPT_REJECTION_QUEUED;
	transition->cancel_main_firmware = true;
	transition->wake_worker = spotflow_ota_attempt_supersede(attempt, constraints);
	transition->current_attempt_id = attempt->identity.id;
	transition->pending_attempt_id = attempt_id;
}

void spotflow_ota_attempt_start(const struct spotflow_ota_update_msg* msg, uint32_t generation,
				struct ota_attempt_model* attempt)
{
	spotflow_ota_attempt_clear(attempt);
	attempt->identity.lifecycle = OTA_ATTEMPT_ACTIVE;
	attempt->identity.id = msg->attempt_id;
	attempt->identity.generation = generation;
	attempt->plan.source = OTA_ARTIFACT_PLAN_FULL_MANIFEST;
	attempt->plan.count = msg->artifact_count;
	memcpy(attempt->plan.artifacts, msg->artifacts,
	       msg->artifact_count * sizeof(attempt->plan.artifacts[0]));
	for (size_t i = 0; i < msg->artifact_count; i++) {
		attempt->plan.results[i] = msg->is_canceled ? SPOTFLOW_OTA_RESULT_CANCELED
							    : SPOTFLOW_OTA_RESULT_PENDING;
	}
	attempt->execution.cancellation_requested = msg->is_canceled;
	spotflow_ota_attempt_advance(attempt);
	spotflow_ota_attempt_refresh_lifecycle(attempt, (struct ota_attempt_constraints){ 0 });
	if (msg->is_canceled) {
		attempt->identity.lifecycle = OTA_ATTEMPT_FINALIZING;
	}
}

int spotflow_ota_attempt_rehydrate(const struct spotflow_ota_update_msg* msg,
				   struct ota_attempt_model* attempt,
				   struct ota_attempt_constraints constraints, bool* request_report,
				   bool* wake_worker)
{
	*request_report = false;
	*wake_worker = false;
	if ((attempt->plan.source == OTA_ARTIFACT_PLAN_PERSISTED_RESULTS ||
	     attempt->plan.source == OTA_ARTIFACT_PLAN_FULL_MANIFEST) &&
	    attempt->plan.count != msg->artifact_count) {
		return -EINVAL;
	}
	if (attempt->plan.source == OTA_ARTIFACT_PLAN_PROBATION_PREFIX &&
	    attempt->plan.count > msg->artifact_count) {
		return -EINVAL;
	}

	size_t previous_count = attempt->plan.count;
	attempt->plan.source = OTA_ARTIFACT_PLAN_FULL_MANIFEST;
	attempt->plan.count = msg->artifact_count;
	memcpy(attempt->plan.artifacts, msg->artifacts,
	       msg->artifact_count * sizeof(attempt->plan.artifacts[0]));
	for (size_t i = previous_count; i < attempt->plan.count; i++) {
		attempt->plan.results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}
	spotflow_ota_attempt_advance(attempt);
	*request_report = spotflow_ota_attempt_has_reportable_results(attempt);
	if (msg->is_canceled && !spotflow_ota_attempt_has_succeeded_artifact(attempt)) {
		attempt->execution.cancellation_requested = true;
		spotflow_ota_attempt_cancel_pending_artifacts(attempt, constraints);
	}
	*wake_worker = spotflow_ota_attempt_has_terminal_results(attempt) ||
		spotflow_ota_attempt_has_runnable_artifact(attempt, constraints);
	spotflow_ota_attempt_refresh_lifecycle(attempt, constraints);
	return 0;
}

void spotflow_ota_attempt_start_rejected(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
					 uint32_t generation, struct ota_attempt_model* attempt)
{
	spotflow_ota_attempt_clear(attempt);
	attempt->identity.lifecycle = OTA_ATTEMPT_REJECTING;
	attempt->identity.id = attempt_id;
	attempt->identity.generation = generation;
	attempt->failure.state = OTA_ATTEMPT_FAILURE_PRESENT;
	attempt->failure.error = error;
}

void spotflow_ota_pending_attempt_clear(struct ota_pending_attempt* pending)
{
	memset(pending, 0, sizeof(*pending));
	pending->kind = OTA_PENDING_ATTEMPT_NONE;
}

void spotflow_ota_pending_attempt_store_update(struct ota_pending_attempt* pending,
					       const struct spotflow_ota_update_msg* msg)
{
	pending->kind = OTA_PENDING_ATTEMPT_UPDATE;
	pending->data.update = *msg;
}

void spotflow_ota_pending_attempt_store_rejection(struct ota_pending_attempt* pending,
						  uint64_t attempt_id,
						  enum spotflow_ota_attempt_error error)
{
	memset(pending, 0, sizeof(*pending));
	pending->kind = OTA_PENDING_ATTEMPT_REJECTION;
	pending->data.rejection.attempt_id = attempt_id;
	pending->data.rejection.error = error;
}

uint64_t spotflow_ota_pending_attempt_id(const struct ota_pending_attempt* pending)
{
	switch (pending->kind) {
	case OTA_PENDING_ATTEMPT_UPDATE:
		return pending->data.update.attempt_id;
	case OTA_PENDING_ATTEMPT_REJECTION:
		return pending->data.rejection.attempt_id;
	case OTA_PENDING_ATTEMPT_NONE:
	default:
		return 0;
	}
}

bool spotflow_ota_attempt_promote_pending(struct ota_attempt_model* attempt,
					  struct ota_pending_attempt* pending, uint32_t generation)
{
	switch (pending->kind) {
	case OTA_PENDING_ATTEMPT_UPDATE:
		spotflow_ota_attempt_start(&pending->data.update, generation, attempt);
		break;
	case OTA_PENDING_ATTEMPT_REJECTION:
		spotflow_ota_attempt_start_rejected(pending->data.rejection.attempt_id,
						    pending->data.rejection.error, generation,
						    attempt);
		break;
	case OTA_PENDING_ATTEMPT_NONE:
	default:
		return false;
	}
	spotflow_ota_pending_attempt_clear(pending);
	return true;
}

struct ota_attempt_cancel_transition
spotflow_ota_attempt_accept_cancel(struct ota_attempt_model* attempt, uint64_t attempt_id,
				   struct ota_attempt_constraints constraints)
{
	struct ota_attempt_cancel_transition transition = {
		.outcome = OTA_ATTEMPT_CANCEL_NOT_CURRENT,
	};
	if (!spotflow_ota_attempt_exists(attempt) || attempt->identity.id != attempt_id) {
		return transition;
	}
	if (spotflow_ota_attempt_has_terminal_results(attempt) ||
	    spotflow_ota_attempt_has_succeeded_artifact(attempt) ||
	    constraints.main_upgrade_irreversible) {
		transition.outcome = OTA_ATTEMPT_CANCEL_IGNORED_LATE;
		return transition;
	}

	attempt->execution.cancellation_requested = true;
	if (spotflow_ota_attempt_result_commit_is_pending(attempt)) {
		attempt->execution.transaction.mutation.cancel_remaining = true;
		spotflow_ota_attempt_advance_mutation_revision(attempt);
	}
	transition.outcome = OTA_ATTEMPT_CANCEL_ACCEPTED;
	transition.wake_worker = !spotflow_ota_attempt_transaction_is_active(attempt);
	if (!spotflow_ota_attempt_transaction_is_active(attempt)) {
		spotflow_ota_attempt_cancel_pending_artifacts(attempt, constraints);
		if (spotflow_ota_attempt_has_terminal_results(attempt)) {
			attempt->identity.lifecycle = OTA_ATTEMPT_FINALIZING;
		}
	}
	return transition;
}

bool spotflow_ota_attempt_supersede(struct ota_attempt_model* attempt,
				    struct ota_attempt_constraints constraints)
{
	if (constraints.main_upgrade_irreversible) {
		return false;
	}
	attempt->execution.cancellation_requested = true;
	if (spotflow_ota_attempt_result_commit_is_pending(attempt)) {
		attempt->execution.transaction.mutation.cancel_remaining = true;
		spotflow_ota_attempt_advance_mutation_revision(attempt);
	} else {
		spotflow_ota_attempt_cancel_pending_artifacts(attempt, constraints);
	}
	if (spotflow_ota_attempt_has_terminal_results(attempt) &&
	    attempt->identity.lifecycle != OTA_ATTEMPT_FINALIZATION_CLAIMED) {
		attempt->identity.lifecycle = OTA_ATTEMPT_FINALIZING;
	}
	return true;
}

bool spotflow_ota_attempt_has_terminal_results(const struct ota_attempt_model* attempt)
{
	if (!spotflow_ota_attempt_exists(attempt) ||
	    attempt->failure.state == OTA_ATTEMPT_FAILURE_PRESENT) {
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

bool spotflow_ota_attempt_has_projected_terminal_results(const struct ota_attempt_model* attempt,
							 struct ota_attempt_constraints constraints)
{
	enum spotflow_ota_result results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	spotflow_ota_attempt_project_results(attempt, constraints, results);
	if (attempt->plan.count == 0) {
		return attempt->failure.state == OTA_ATTEMPT_FAILURE_PRESENT;
	}
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			return false;
		}
	}
	return true;
}

bool spotflow_ota_attempt_has_reportable_results(const struct ota_attempt_model* attempt)
{
	if (!spotflow_ota_attempt_exists(attempt)) {
		return false;
	}
	if (attempt->failure.state == OTA_ATTEMPT_FAILURE_PRESENT) {
		return true;
	}
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] != SPOTFLOW_OTA_RESULT_PENDING) {
			return true;
		}
	}
	return false;
}

bool spotflow_ota_attempt_has_succeeded_artifact(const struct ota_attempt_model* attempt)
{
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
			return true;
		}
	}
	return false;
}

bool spotflow_ota_attempt_has_runnable_artifact(const struct ota_attempt_model* attempt,
						struct ota_attempt_constraints constraints)
{
	return spotflow_ota_attempt_exists(attempt) &&
		attempt->plan.source == OTA_ARTIFACT_PLAN_FULL_MANIFEST &&
		attempt->execution.sequence_policy == OTA_ARTIFACT_SEQUENCE_CONTINUE &&
		!attempt->execution.cancellation_requested &&
		!spotflow_ota_attempt_transaction_is_active(attempt) &&
		!constraints.probation_artifact_pending &&
		attempt->execution.next_index < attempt->plan.count &&
		attempt->plan.results[attempt->execution.next_index] == SPOTFLOW_OTA_RESULT_PENDING;
}

void spotflow_ota_attempt_stage_result(struct ota_attempt_model* attempt, size_t artifact_index,
				       enum spotflow_ota_result result,
				       enum ota_artifact_result_source source)
{
	bool stop_remaining = result == SPOTFLOW_OTA_RESULT_FAILED ||
		result == SPOTFLOW_OTA_RESULT_CANCELED ||
		attempt->execution.sequence_policy == OTA_ARTIFACT_SEQUENCE_STOP_REMAINING;
	if (attempt->execution.cancellation_requested && result == SPOTFLOW_OTA_RESULT_SUCCEEDED &&
	    !stop_remaining) {
		attempt->execution.cancellation_requested = false;
	}
	attempt->execution.transaction.state = OTA_ARTIFACT_TRANSACTION_RESULT_STAGED;
	attempt->execution.transaction.artifact_index = artifact_index;
	attempt->execution.transaction.mutation_revision = 1;
	attempt->execution.transaction.mutation = (struct ota_artifact_result_mutation){
		.artifact_index = artifact_index,
		.result = result,
		.cancel_remaining = stop_remaining || attempt->execution.cancellation_requested,
		.source = source,
	};
}

void spotflow_ota_attempt_apply_staged_result(struct ota_attempt_model* attempt,
					      struct ota_attempt_constraints constraints)
{
	const struct ota_artifact_result_mutation mutation =
		attempt->execution.transaction.mutation;
	attempt->plan.results[mutation.artifact_index] = mutation.result;
	if (mutation.cancel_remaining) {
		attempt->execution.sequence_policy = OTA_ARTIFACT_SEQUENCE_STOP_REMAINING;
		cancel_pending_results(attempt, constraints, attempt->plan.results);
	}
	spotflow_ota_attempt_clear_transaction(attempt);
	spotflow_ota_attempt_advance(attempt);
}

void spotflow_ota_attempt_project_results(const struct ota_attempt_model* attempt,
					  struct ota_attempt_constraints constraints,
					  enum spotflow_ota_result* results)
{
	memcpy(results, attempt->plan.results, sizeof(attempt->plan.results));
	if (!spotflow_ota_attempt_result_commit_is_pending(attempt)) {
		return;
	}
	results[attempt->execution.transaction.mutation.artifact_index] =
		attempt->execution.transaction.mutation.result;
	if (attempt->execution.transaction.mutation.cancel_remaining) {
		cancel_pending_results(attempt, constraints, results);
	}
}

void spotflow_ota_attempt_cancel_pending_artifacts(struct ota_attempt_model* attempt,
						   struct ota_attempt_constraints constraints)
{
	cancel_pending_results(attempt, constraints, attempt->plan.results);
	spotflow_ota_attempt_advance(attempt);
}

void spotflow_ota_attempt_refresh_lifecycle(struct ota_attempt_model* attempt,
					    struct ota_attempt_constraints constraints)
{
	if (!spotflow_ota_attempt_exists(attempt) ||
	    attempt->identity.lifecycle == OTA_ATTEMPT_REJECTING ||
	    attempt->identity.lifecycle == OTA_ATTEMPT_REJECTION_CLAIMED ||
	    attempt->identity.lifecycle == OTA_ATTEMPT_REJECTED ||
	    attempt->identity.lifecycle == OTA_ATTEMPT_FINALIZATION_CLAIMED) {
		return;
	}
	if (attempt->failure.state == OTA_ATTEMPT_FAILURE_PRESENT) {
		attempt->identity.lifecycle = OTA_ATTEMPT_REJECTED;
	} else if (spotflow_ota_attempt_result_commit_is_pending(attempt)) {
		attempt->identity.lifecycle =
			spotflow_ota_attempt_has_projected_terminal_results(attempt, constraints)
			? OTA_ATTEMPT_FINALIZING
			: OTA_ATTEMPT_ACTIVE;
	} else if (spotflow_ota_attempt_has_terminal_results(attempt)) {
		attempt->identity.lifecycle = OTA_ATTEMPT_TERMINAL;
	} else if (attempt->plan.source == OTA_ARTIFACT_PLAN_FULL_MANIFEST) {
		attempt->identity.lifecycle = OTA_ATTEMPT_ACTIVE;
	} else {
		attempt->identity.lifecycle = OTA_ATTEMPT_AWAITING_MANIFEST;
	}
}

void spotflow_ota_attempt_clear_transaction(struct ota_attempt_model* attempt)
{
	memset(&attempt->execution.transaction, 0, sizeof(attempt->execution.transaction));
	attempt->execution.transaction.state = OTA_ARTIFACT_TRANSACTION_IDLE;
}

void spotflow_ota_attempt_advance_mutation_revision(struct ota_attempt_model* attempt)
{
	attempt->execution.transaction.mutation_revision++;
	if (attempt->execution.transaction.mutation_revision == 0) {
		attempt->execution.transaction.mutation_revision++;
	}
}

void spotflow_ota_attempt_advance(struct ota_attempt_model* attempt)
{
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (attempt->plan.results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			attempt->execution.next_index = i;
			return;
		}
	}
	attempt->execution.next_index = attempt->plan.count;
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
static void cancel_pending_results(const struct ota_attempt_model* attempt,
				   struct ota_attempt_constraints constraints,
				   enum spotflow_ota_result* results)
{
	for (size_t i = 0; i < attempt->plan.count; i++) {
		if (spotflow_ota_attempt_transaction_is_active(attempt) &&
		    i == attempt->execution.transaction.artifact_index) {
			continue;
		}
		if (constraints.probation_artifact_pending &&
		    i == constraints.probation_artifact_index) {
			continue;
		}
		if (results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			results[i] = SPOTFLOW_OTA_RESULT_CANCELED;
		}
	}
}
