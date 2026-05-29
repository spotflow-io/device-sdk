#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "ota/spotflow_ota_state.h"

struct attempt_state {
	bool active;
	uint64_t attempt_id;
	struct spotflow_ota_update_msg update;
	enum spotflow_ota_result results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	size_t current_artifact_index;
	bool artifact_running;
	size_t running_artifact_index;
	bool actionable_cancellation;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	bool rejected_job_pending;
	struct spotflow_ota_main_firmware_state main_firmware_state;
};

static struct attempt_state current_attempt;
static struct spotflow_ota_update_msg pending_update;
static bool has_pending_update;
static K_MUTEX_DEFINE(state_mutex);

static void clear_action(struct spotflow_ota_state_action* action);
static void clear_worker_job(struct spotflow_ota_worker_job* job);
static void clear_attempt(struct attempt_state* attempt);
static int validate_update_msg(const struct spotflow_ota_update_msg* msg);
static void start_attempt(const struct spotflow_ota_update_msg* msg, struct attempt_state* attempt);
static void store_pending_update(const struct spotflow_ota_update_msg* msg);
static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source);
static bool attempt_has_terminal_results(const struct attempt_state* attempt);
static bool attempt_has_succeeded_artifact(const struct attempt_state* attempt);
static void cancel_pending_artifacts(struct attempt_state* attempt);
static void advance_current_artifact(struct attempt_state* attempt);
static void fill_action(struct spotflow_ota_state_action* action, uint64_t attempt_id);

void spotflow_ota_state_reset(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	clear_attempt(&current_attempt);
	memset(&pending_update, 0, sizeof(pending_update));
	has_pending_update = false;
	k_mutex_unlock(&state_mutex);
}

int spotflow_ota_state_accept_update(const struct spotflow_ota_update_msg* msg,
				     struct spotflow_ota_state_action* action)
{
	clear_action(action);

	int rc = validate_update_msg(msg);
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!current_attempt.active || attempt_has_terminal_results(&current_attempt)) {
		start_attempt(msg, &current_attempt);
		has_pending_update = false;
		fill_action(action, msg->attempt_id);
		action->accepted_update = true;
		action->wake_worker = !attempt_has_terminal_results(&current_attempt);
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	if (current_attempt.attempt_id == msg->attempt_id) {
		fill_action(action, msg->attempt_id);
		action->ignored_duplicate_update = true;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	store_pending_update(msg);
	current_attempt.actionable_cancellation = true;
	cancel_pending_artifacts(&current_attempt);
	fill_action(action, current_attempt.attempt_id);
	action->superseded_current = true;
	action->wake_worker = true;
	action->can_promote_pending = attempt_has_terminal_results(&current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_reject_update(uint64_t attempt_id, enum spotflow_ota_attempt_error error,
				     struct spotflow_ota_state_action* action)
{
	clear_action(action);

	if (attempt_id == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	clear_attempt(&current_attempt);
	current_attempt.active = true;
	current_attempt.attempt_id = attempt_id;
	current_attempt.has_attempt_error = true;
	current_attempt.attempt_error = error;
	current_attempt.rejected_job_pending = true;
	has_pending_update = false;

	fill_action(action, attempt_id);
	action->rejected_attempt = true;
	action->wake_worker = true;

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_cancel(uint64_t attempt_id, struct spotflow_ota_state_action* action)
{
	clear_action(action);

	if (attempt_id == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!current_attempt.active || current_attempt.attempt_id != attempt_id) {
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	fill_action(action, attempt_id);

	if (attempt_has_terminal_results(&current_attempt) ||
	    attempt_has_succeeded_artifact(&current_attempt)) {
		action->ignored_late_cancel = true;
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	current_attempt.actionable_cancellation = true;
	cancel_pending_artifacts(&current_attempt);
	action->accepted_cancel = true;
	action->wake_worker = true;
	action->can_promote_pending =
	    has_pending_update && attempt_has_terminal_results(&current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_accept_report_request(uint64_t attempt_id,
					     struct spotflow_ota_state_action* action)
{
	clear_action(action);

	if (attempt_id == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (current_attempt.active && current_attempt.attempt_id == attempt_id) {
		fill_action(action, attempt_id);
		action->report_requested = true;
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

	if (!current_attempt.active) {
		k_mutex_unlock(&state_mutex);
		return false;
	}

	if (current_attempt.rejected_job_pending) {
		job->type = SPOTFLOW_OTA_WORKER_JOB_REJECTED_ATTEMPT;
		job->attempt_id = current_attempt.attempt_id;
		job->attempt_error = current_attempt.attempt_error;
		current_attempt.rejected_job_pending = false;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	if (!current_attempt.actionable_cancellation && !current_attempt.artifact_running &&
	    current_attempt.current_artifact_index < current_attempt.update.artifact_count &&
	    current_attempt.results[current_attempt.current_artifact_index] ==
		SPOTFLOW_OTA_RESULT_PENDING) {
		size_t index = current_attempt.current_artifact_index;

		job->type = SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT;
		job->attempt_id = current_attempt.attempt_id;
		job->artifact_index = index;
		copy_artifact_out(&job->artifact, &current_attempt.update.artifacts[index]);
		current_attempt.artifact_running = true;
		current_attempt.running_artifact_index = index;
		k_mutex_unlock(&state_mutex);
		return true;
	}

	k_mutex_unlock(&state_mutex);
	return false;
}

int spotflow_ota_state_apply_artifact_result(size_t artifact_index, enum spotflow_ota_result result,
					     struct spotflow_ota_state_action* action)
{
	clear_action(action);

	if (result == SPOTFLOW_OTA_RESULT_PENDING) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!current_attempt.active || artifact_index >= current_attempt.update.artifact_count) {
		k_mutex_unlock(&state_mutex);
		return -EINVAL;
	}

	if (current_attempt.results[artifact_index] != SPOTFLOW_OTA_RESULT_PENDING) {
		fill_action(action, current_attempt.attempt_id);
		action->can_promote_pending =
		    has_pending_update && attempt_has_terminal_results(&current_attempt);
		k_mutex_unlock(&state_mutex);
		return 0;
	}

	current_attempt.results[artifact_index] = result;
	current_attempt.artifact_running = false;
	fill_action(action, current_attempt.attempt_id);

	if (current_attempt.actionable_cancellation || result == SPOTFLOW_OTA_RESULT_FAILED ||
	    result == SPOTFLOW_OTA_RESULT_CANCELED) {
		current_attempt.actionable_cancellation = true;
		cancel_pending_artifacts(&current_attempt);
	}

	advance_current_artifact(&current_attempt);
	action->wake_worker = !attempt_has_terminal_results(&current_attempt);
	action->can_promote_pending =
	    has_pending_update && attempt_has_terminal_results(&current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

int spotflow_ota_state_promote_pending(struct spotflow_ota_state_action* action)
{
	clear_action(action);

	k_mutex_lock(&state_mutex, K_FOREVER);

	if (!has_pending_update || !attempt_has_terminal_results(&current_attempt)) {
		k_mutex_unlock(&state_mutex);
		return -EAGAIN;
	}

	start_attempt(&pending_update, &current_attempt);
	has_pending_update = false;
	fill_action(action, current_attempt.attempt_id);
	action->promoted_pending = true;
	action->wake_worker = !attempt_has_terminal_results(&current_attempt);

	k_mutex_unlock(&state_mutex);
	return 0;
}

bool spotflow_ota_state_is_update_canceled(void)
{
	bool canceled;

	k_mutex_lock(&state_mutex, K_FOREVER);
	canceled = current_attempt.active && current_attempt.actionable_cancellation;
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

	snapshot->has_current_attempt = current_attempt.active;
	snapshot->current_attempt_id = current_attempt.attempt_id;
	snapshot->artifact_count = current_attempt.update.artifact_count;
	snapshot->current_artifact_index = current_attempt.current_artifact_index;
	snapshot->actionable_cancellation = current_attempt.actionable_cancellation;
	snapshot->has_pending_attempt = has_pending_update;
	snapshot->pending_attempt_id = pending_update.attempt_id;
	snapshot->has_attempt_error = current_attempt.has_attempt_error;
	snapshot->attempt_error = current_attempt.attempt_error;
	snapshot->main_firmware_state = current_attempt.main_firmware_state;
	memcpy(snapshot->artifact_results, current_attempt.results,
	       sizeof(snapshot->artifact_results));

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
	attempt->main_firmware_state.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	attempt->main_firmware_state.result = SPOTFLOW_OTA_RESULT_PENDING;
}

static int validate_update_msg(const struct spotflow_ota_update_msg* msg)
{
	if (msg == NULL || msg->attempt_id == 0 || msg->artifact_count == 0 ||
	    msg->artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	return 0;
}

static void start_attempt(const struct spotflow_ota_update_msg* msg, struct attempt_state* attempt)
{
	clear_attempt(attempt);
	attempt->active = true;
	attempt->attempt_id = msg->attempt_id;
	attempt->update = *msg;

	for (size_t i = 0; i < msg->artifact_count; i++) {
		attempt->results[i] =
		    msg->is_canceled ? SPOTFLOW_OTA_RESULT_CANCELED : SPOTFLOW_OTA_RESULT_PENDING;
	}

	attempt->actionable_cancellation = msg->is_canceled;
	advance_current_artifact(attempt);
}

static void store_pending_update(const struct spotflow_ota_update_msg* msg)
{
	pending_update = *msg;
	has_pending_update = true;
}

static void copy_artifact_out(struct spotflow_ota_artifact* destination,
			      const struct spotflow_ota_artifact* source)
{
	*destination = *source;
}

static bool attempt_has_terminal_results(const struct attempt_state* attempt)
{
	if (!attempt->active) {
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

static bool attempt_has_succeeded_artifact(const struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (attempt->results[i] == SPOTFLOW_OTA_RESULT_SUCCEEDED) {
			return true;
		}
	}

	return false;
}

static void cancel_pending_artifacts(struct attempt_state* attempt)
{
	for (size_t i = 0; i < attempt->update.artifact_count; i++) {
		if (attempt->artifact_running && i == attempt->running_artifact_index) {
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
	if (action != NULL) {
		action->attempt_id = attempt_id;
	}
}
