#include "ota/core/spotflow_ota_main_model.h"

#include <errno.h>
#include <string.h>

#include "ota/persistence/spotflow_ota_records_cbor.h"

static bool execution_allows_pause(enum ota_main_execution_state execution);
static bool execution_allows_abort(enum ota_main_execution_state execution);
static enum spotflow_ota_phase execution_phase(enum ota_main_execution_state execution);
static bool result_is_reconciled(enum spotflow_ota_result result);

void spotflow_ota_main_clear(struct ota_main_firmware_model* main)
{
	memset(main, 0, sizeof(*main));
	main->presence = OTA_MAIN_FIRMWARE_ABSENT;
	main->execution = OTA_MAIN_EXECUTION_IDLE;
	main->result = SPOTFLOW_OTA_RESULT_PENDING;
	main->probation.state = OTA_MAIN_PROBATION_NONE;
}

bool spotflow_ota_main_is_valid(const struct ota_main_firmware_model* main)
{
	if (main->presence > OTA_MAIN_FIRMWARE_PRESENT ||
	    main->execution > OTA_MAIN_EXECUTION_UNCONFIRMED ||
	    main->result > SPOTFLOW_OTA_RESULT_CANCELED ||
	    main->probation.state > OTA_MAIN_PROBATION_COMPLETION_CLAIMED) {
		return false;
	}
	if (main->presence == OTA_MAIN_FIRMWARE_ABSENT &&
	    (main->execution != OTA_MAIN_EXECUTION_IDLE || main->is_paused ||
	     main->abort_requested || main->probation.state != OTA_MAIN_PROBATION_NONE)) {
		return false;
	}
	if (main->presence == OTA_MAIN_FIRMWARE_PRESENT && !main->artifact.is_main) {
		return false;
	}
	if (main->is_paused && !execution_allows_pause(main->execution)) {
		return false;
	}
	if (main->abort_requested && !execution_allows_abort(main->execution)) {
		return false;
	}

	switch (main->execution) {
	case OTA_MAIN_EXECUTION_IDLE:
		if (main->is_paused || main->abort_requested) {
			return false;
		}
		switch (main->probation.state) {
		case OTA_MAIN_PROBATION_NONE:
			return true;
		case OTA_MAIN_PROBATION_PENDING:
			return main->result == SPOTFLOW_OTA_RESULT_PENDING;
		case OTA_MAIN_PROBATION_COMPLETION_QUEUED:
		case OTA_MAIN_PROBATION_COMPLETION_CLAIMED:
			return result_is_reconciled(main->result);
		default:
			return false;
		}
	case OTA_MAIN_EXECUTION_CLAIMED:
	case OTA_MAIN_EXECUTION_PENDING_DOWNLOAD:
	case OTA_MAIN_EXECUTION_DOWNLOADING:
	case OTA_MAIN_EXECUTION_PENDING_UPGRADE:
	case OTA_MAIN_EXECUTION_COMMITTING:
		return main->presence == OTA_MAIN_FIRMWARE_PRESENT &&
			main->probation.state == OTA_MAIN_PROBATION_NONE &&
			main->result == SPOTFLOW_OTA_RESULT_PENDING;
	case OTA_MAIN_EXECUTION_REBOOT_READY:
		return main->presence == OTA_MAIN_FIRMWARE_PRESENT &&
			main->probation.state == OTA_MAIN_PROBATION_PENDING &&
			main->result == SPOTFLOW_OTA_RESULT_PENDING && !main->abort_requested;
	case OTA_MAIN_EXECUTION_REBOOT_STARTED:
		return main->presence == OTA_MAIN_FIRMWARE_PRESENT &&
			main->probation.state == OTA_MAIN_PROBATION_PENDING &&
			main->result == SPOTFLOW_OTA_RESULT_PENDING && !main->is_paused &&
			!main->abort_requested;
	case OTA_MAIN_EXECUTION_UNCONFIRMED:
		return main->presence == OTA_MAIN_FIRMWARE_PRESENT &&
			main->probation.state == OTA_MAIN_PROBATION_PENDING &&
			main->result == SPOTFLOW_OTA_RESULT_PENDING && !main->is_paused &&
			!main->abort_requested;
	default:
		return false;
	}
}

void spotflow_ota_main_project_state(const struct ota_main_firmware_model* main,
				     struct spotflow_ota_main_firmware_state* out_state)
{
	if (out_state == NULL) {
		return;
	}
	*out_state = (struct spotflow_ota_main_firmware_state){
		.phase = execution_phase(main->execution),
		.is_paused = main->is_paused,
		.result = main->result,
	};
}

bool spotflow_ota_main_probation_is_pending(const struct ota_main_firmware_model* main)
{
	return main->probation.state != OTA_MAIN_PROBATION_NONE;
}

bool spotflow_ota_main_upgrade_is_irreversible(const struct ota_main_firmware_model* main)
{
	return main->execution == OTA_MAIN_EXECUTION_COMMITTING ||
		main->execution == OTA_MAIN_EXECUTION_REBOOT_READY ||
		main->execution == OTA_MAIN_EXECUTION_REBOOT_STARTED;
}

void spotflow_ota_main_restore_probation(struct ota_main_firmware_model* main,
					 const struct spotflow_ota_probation* probation,
					 bool result_pending)
{
	spotflow_ota_main_clear(main);
	main->presence = OTA_MAIN_FIRMWARE_PRESENT;
	main->artifact_index = probation->artifact_index;
	main->artifact.is_main = true;
	strncpy(main->artifact.slug, probation->slug, sizeof(main->artifact.slug) - 1);
	strncpy(main->artifact.version, probation->version, sizeof(main->artifact.version) - 1);
	if (result_pending) {
		main->probation.state = OTA_MAIN_PROBATION_PENDING;
	}
}

int spotflow_ota_main_claim(struct ota_main_firmware_model* main, size_t artifact_index,
			    const struct spotflow_ota_artifact* artifact, bool handler_owned,
			    struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || artifact == NULL || !artifact->is_main ||
	    main->execution != OTA_MAIN_EXECUTION_IDLE ||
	    main->result != SPOTFLOW_OTA_RESULT_PENDING ||
	    main->probation.state != OTA_MAIN_PROBATION_NONE) {
		return -EINVAL;
	}
	main->presence = OTA_MAIN_FIRMWARE_PRESENT;
	main->artifact_index = artifact_index;
	main->artifact = *artifact;
	main->execution = OTA_MAIN_EXECUTION_CLAIMED;
	main->is_paused = false;
	main->abort_requested = false;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

int spotflow_ota_main_download_pending(struct ota_main_firmware_model* main, bool handler_owned,
				       struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || main->execution != OTA_MAIN_EXECUTION_CLAIMED) {
		return -EINVAL;
	}
	main->execution = OTA_MAIN_EXECUTION_PENDING_DOWNLOAD;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

int spotflow_ota_main_download_started(struct ota_main_firmware_model* main, bool handler_owned,
				       struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || main->execution != OTA_MAIN_EXECUTION_PENDING_DOWNLOAD) {
		return -EINVAL;
	}
	main->execution = OTA_MAIN_EXECUTION_DOWNLOADING;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

int spotflow_ota_main_download_completed(struct ota_main_firmware_model* main, bool handler_owned,
					 struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || main->execution != OTA_MAIN_EXECUTION_DOWNLOADING) {
		return -EINVAL;
	}
	main->execution = OTA_MAIN_EXECUTION_PENDING_UPGRADE;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

int spotflow_ota_main_fail_handler(struct ota_main_firmware_model* main,
				   struct spotflow_ota_main_firmware_state* out_state)
{
	if (main->presence != OTA_MAIN_FIRMWARE_PRESENT ||
	    (main->execution != OTA_MAIN_EXECUTION_CLAIMED &&
	     main->execution != OTA_MAIN_EXECUTION_PENDING_DOWNLOAD &&
	     main->execution != OTA_MAIN_EXECUTION_DOWNLOADING &&
	     main->execution != OTA_MAIN_EXECUTION_PENDING_UPGRADE &&
	     main->execution != OTA_MAIN_EXECUTION_COMMITTING) ||
	    main->probation.state != OTA_MAIN_PROBATION_NONE) {
		return -EINVAL;
	}
	spotflow_ota_main_finish_handler(main, SPOTFLOW_OTA_RESULT_FAILED);
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

void spotflow_ota_main_finish_handler(struct ota_main_firmware_model* main,
				      enum spotflow_ota_result result)
{
	main->execution = OTA_MAIN_EXECUTION_IDLE;
	main->result = result == SPOTFLOW_OTA_RESULT_PENDING ? SPOTFLOW_OTA_RESULT_FAILED : result;
	main->is_paused = false;
	main->abort_requested = false;
}

void spotflow_ota_main_fail_operation(struct ota_main_firmware_model* main)
{
	spotflow_ota_main_finish_handler(main, SPOTFLOW_OTA_RESULT_FAILED);
}

int spotflow_ota_main_queue_reconciled_result(struct ota_main_firmware_model* main,
					      size_t artifact_index,
					      enum spotflow_ota_result result,
					      struct spotflow_ota_main_firmware_state* out_state)
{
	if ((result != SPOTFLOW_OTA_RESULT_SUCCEEDED && result != SPOTFLOW_OTA_RESULT_FAILED) ||
	    main->probation.state != OTA_MAIN_PROBATION_PENDING ||
	    main->presence != OTA_MAIN_FIRMWARE_PRESENT || main->artifact_index != artifact_index ||
	    (main->execution != OTA_MAIN_EXECUTION_IDLE &&
	     main->execution != OTA_MAIN_EXECUTION_UNCONFIRMED)) {
		return -EINVAL;
	}
	spotflow_ota_main_finish_handler(main, result);
	main->probation.state = OTA_MAIN_PROBATION_COMPLETION_QUEUED;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

bool spotflow_ota_main_claim_completion(struct ota_main_firmware_model* main,
					struct ota_main_completion_transition* transition)
{
	if (transition == NULL || main->probation.state != OTA_MAIN_PROBATION_COMPLETION_QUEUED ||
	    main->execution != OTA_MAIN_EXECUTION_IDLE || !result_is_reconciled(main->result)) {
		return false;
	}
	*transition = (struct ota_main_completion_transition){
		.artifact_index = main->artifact_index,
		.artifact = main->artifact,
		.result = main->result,
		.claim_artifact_transaction = true,
	};
	main->probation.state = OTA_MAIN_PROBATION_COMPLETION_CLAIMED;
	return true;
}

int spotflow_ota_main_commit_probation_cleared(struct ota_main_firmware_model* main,
					       bool reconciliation_owned)
{
	if (!reconciliation_owned ||
	    main->probation.state != OTA_MAIN_PROBATION_COMPLETION_CLAIMED ||
	    main->execution != OTA_MAIN_EXECUTION_IDLE || !result_is_reconciled(main->result)) {
		return -EINVAL;
	}
	main->probation.state = OTA_MAIN_PROBATION_NONE;
	return 0;
}

int spotflow_ota_main_finish_prereboot(struct ota_main_firmware_model* main, bool handler_owned,
				       struct ota_main_prereboot_transition* transition)
{
	if (!handler_owned || transition == NULL ||
	    main->execution != OTA_MAIN_EXECUTION_COMMITTING) {
		return -EINVAL;
	}
	main->probation.state = OTA_MAIN_PROBATION_PENDING;
	main->execution = OTA_MAIN_EXECUTION_REBOOT_READY;
	spotflow_ota_main_project_state(main, &transition->state);
	transition->clear_artifact_transaction = true;
	return 0;
}

int spotflow_ota_main_enter_unconfirmed(struct ota_main_firmware_model* main, size_t artifact_index,
					struct spotflow_ota_main_firmware_state* out_state)
{
	if (main->probation.state != OTA_MAIN_PROBATION_PENDING ||
	    main->presence != OTA_MAIN_FIRMWARE_PRESENT || main->artifact_index != artifact_index ||
	    main->execution != OTA_MAIN_EXECUTION_IDLE) {
		return -EINVAL;
	}
	main->execution = OTA_MAIN_EXECUTION_UNCONFIRMED;
	main->is_paused = false;
	main->abort_requested = false;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

int spotflow_ota_main_set_paused(struct ota_main_firmware_model* main, bool has_attempt,
				 bool paused, struct spotflow_ota_main_firmware_state* out_state)
{
	if (!has_attempt || (paused && !execution_allows_pause(main->execution)) ||
	    (!paused && !main->is_paused)) {
		return -EINVAL;
	}
	main->is_paused = paused;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

int spotflow_ota_main_request_abort(struct ota_main_firmware_model* main, bool has_attempt,
				    struct spotflow_ota_main_firmware_state* out_state)
{
	if (!has_attempt || !execution_allows_abort(main->execution)) {
		spotflow_ota_main_project_state(main, out_state);
		return -EINVAL;
	}
	main->abort_requested = true;
	main->is_paused = false;
	spotflow_ota_main_project_state(main, out_state);
	return 0;
}

bool spotflow_ota_main_is_abort_requested(const struct ota_main_firmware_model* main,
					  bool has_attempt)
{
	return has_attempt && main->abort_requested;
}

int spotflow_ota_main_begin_upgrade_commit(struct ota_main_firmware_model* main, bool handler_owned,
					   bool attempt_canceled)
{
	if (!handler_owned || main->execution != OTA_MAIN_EXECUTION_PENDING_UPGRADE) {
		return -EINVAL;
	}
	if (main->abort_requested || attempt_canceled) {
		return -ECANCELED;
	}
	if (main->is_paused) {
		return -EAGAIN;
	}
	main->execution = OTA_MAIN_EXECUTION_COMMITTING;
	return 0;
}

int spotflow_ota_main_cancel_upgrade_commit(struct ota_main_firmware_model* main)
{
	if (main->execution != OTA_MAIN_EXECUTION_COMMITTING) {
		return -EINVAL;
	}
	main->execution = OTA_MAIN_EXECUTION_PENDING_UPGRADE;
	return 0;
}

int spotflow_ota_main_begin_reboot(struct ota_main_firmware_model* main)
{
	if (main->execution != OTA_MAIN_EXECUTION_REBOOT_READY ||
	    main->probation.state != OTA_MAIN_PROBATION_PENDING) {
		return -EINVAL;
	}
	if (main->is_paused) {
		return -EAGAIN;
	}
	main->execution = OTA_MAIN_EXECUTION_REBOOT_STARTED;
	return 0;
}

static bool execution_allows_pause(enum ota_main_execution_state execution)
{
	return execution == OTA_MAIN_EXECUTION_PENDING_DOWNLOAD ||
		execution == OTA_MAIN_EXECUTION_DOWNLOADING ||
		execution == OTA_MAIN_EXECUTION_PENDING_UPGRADE ||
		execution == OTA_MAIN_EXECUTION_COMMITTING ||
		execution == OTA_MAIN_EXECUTION_REBOOT_READY;
}

static bool execution_allows_abort(enum ota_main_execution_state execution)
{
	return execution == OTA_MAIN_EXECUTION_PENDING_DOWNLOAD ||
		execution == OTA_MAIN_EXECUTION_DOWNLOADING ||
		execution == OTA_MAIN_EXECUTION_PENDING_UPGRADE;
}

static enum spotflow_ota_phase execution_phase(enum ota_main_execution_state execution)
{
	switch (execution) {
	case OTA_MAIN_EXECUTION_PENDING_DOWNLOAD:
		return SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD;
	case OTA_MAIN_EXECUTION_DOWNLOADING:
		return SPOTFLOW_OTA_PHASE_DOWNLOADING;
	case OTA_MAIN_EXECUTION_PENDING_UPGRADE:
	case OTA_MAIN_EXECUTION_COMMITTING:
		return SPOTFLOW_OTA_PHASE_PENDING_UPGRADE;
	case OTA_MAIN_EXECUTION_REBOOT_READY:
	case OTA_MAIN_EXECUTION_REBOOT_STARTED:
		return SPOTFLOW_OTA_PHASE_PENDING_REBOOT;
	case OTA_MAIN_EXECUTION_UNCONFIRMED:
		return SPOTFLOW_OTA_PHASE_UNCONFIRMED;
	case OTA_MAIN_EXECUTION_IDLE:
	case OTA_MAIN_EXECUTION_CLAIMED:
	default:
		return SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	}
}

static bool result_is_reconciled(enum spotflow_ota_result result)
{
	return result == SPOTFLOW_OTA_RESULT_SUCCEEDED || result == SPOTFLOW_OTA_RESULT_FAILED;
}
