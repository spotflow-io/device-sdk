#include "ota/core/spotflow_ota_main_model.h"

#include <errno.h>
#include <string.h>

#include "ota/persistence/spotflow_ota_records_cbor.h"

static bool phase_allows_pause(enum spotflow_ota_phase phase);
static bool phase_allows_abort(enum spotflow_ota_phase phase);
static void copy_state(const struct ota_main_firmware_model* main,
		       struct spotflow_ota_main_firmware_state* out_state);

void spotflow_ota_main_clear(struct ota_main_firmware_model* main)
{
	memset(main, 0, sizeof(*main));
	main->presence = OTA_MAIN_FIRMWARE_ABSENT;
	main->upgrade = OTA_MAIN_UPGRADE_IDLE;
	main->probation.state = OTA_MAIN_PROBATION_NONE;
	main->status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	main->status.result = SPOTFLOW_OTA_RESULT_PENDING;
}

bool spotflow_ota_main_is_valid(const struct ota_main_firmware_model* main)
{
	switch (main->upgrade) {
	case OTA_MAIN_UPGRADE_IDLE:
		if (main->status.phase == SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD ||
		    main->status.phase == SPOTFLOW_OTA_PHASE_DOWNLOADING ||
		    main->status.phase == SPOTFLOW_OTA_PHASE_PENDING_UPGRADE ||
		    main->status.phase == SPOTFLOW_OTA_PHASE_PENDING_REBOOT) {
			return false;
		}
		break;
	case OTA_MAIN_UPGRADE_HANDLER_ACTIVE:
		if (main->probation.state != OTA_MAIN_PROBATION_NONE ||
		    (main->status.phase != SPOTFLOW_OTA_PHASE_NOT_RUNNING &&
		     main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD &&
		     main->status.phase != SPOTFLOW_OTA_PHASE_DOWNLOADING &&
		     main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_UPGRADE)) {
			return false;
		}
		break;
	case OTA_MAIN_UPGRADE_COMMITTING:
		if (main->probation.state != OTA_MAIN_PROBATION_NONE ||
		    main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_UPGRADE) {
			return false;
		}
		break;
	case OTA_MAIN_UPGRADE_REBOOT_READY:
	case OTA_MAIN_UPGRADE_REBOOT_STARTED:
		if (main->probation.state != OTA_MAIN_PROBATION_PENDING ||
		    main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_REBOOT) {
			return false;
		}
		break;
	default:
		return false;
	}

	if ((main->probation.state == OTA_MAIN_PROBATION_COMPLETION_QUEUED ||
	     main->probation.state == OTA_MAIN_PROBATION_COMPLETION_CLAIMED) &&
	    (main->upgrade != OTA_MAIN_UPGRADE_IDLE ||
	     main->status.phase != SPOTFLOW_OTA_PHASE_NOT_RUNNING ||
	     (main->status.result != SPOTFLOW_OTA_RESULT_SUCCEEDED &&
	      main->status.result != SPOTFLOW_OTA_RESULT_FAILED))) {
		return false;
	}
	return true;
}

bool spotflow_ota_main_probation_is_pending(const struct ota_main_firmware_model* main)
{
	return main->probation.state != OTA_MAIN_PROBATION_NONE;
}

bool spotflow_ota_main_upgrade_is_irreversible(const struct ota_main_firmware_model* main)
{
	return main->upgrade == OTA_MAIN_UPGRADE_COMMITTING ||
		main->upgrade == OTA_MAIN_UPGRADE_REBOOT_READY ||
		main->upgrade == OTA_MAIN_UPGRADE_REBOOT_STARTED;
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
	    main->upgrade != OTA_MAIN_UPGRADE_IDLE ||
	    main->probation.state != OTA_MAIN_PROBATION_NONE) {
		return -EINVAL;
	}
	main->presence = OTA_MAIN_FIRMWARE_PRESENT;
	main->artifact_index = artifact_index;
	main->artifact = *artifact;
	main->status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	main->status.result = SPOTFLOW_OTA_RESULT_PENDING;
	main->status.is_paused = false;
	main->abort_requested = false;
	main->upgrade = OTA_MAIN_UPGRADE_HANDLER_ACTIVE;
	copy_state(main, out_state);
	return 0;
}

int spotflow_ota_main_download_pending(struct ota_main_firmware_model* main, bool handler_owned,
				       struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || main->upgrade != OTA_MAIN_UPGRADE_HANDLER_ACTIVE ||
	    main->status.phase != SPOTFLOW_OTA_PHASE_NOT_RUNNING) {
		return -EINVAL;
	}
	main->status.phase = SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD;
	copy_state(main, out_state);
	return 0;
}

int spotflow_ota_main_download_started(struct ota_main_firmware_model* main, bool handler_owned,
				       struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD) {
		return -EINVAL;
	}
	main->status.phase = SPOTFLOW_OTA_PHASE_DOWNLOADING;
	copy_state(main, out_state);
	return 0;
}

int spotflow_ota_main_download_completed(struct ota_main_firmware_model* main, bool handler_owned,
					 struct spotflow_ota_main_firmware_state* out_state)
{
	if (!handler_owned || main->status.phase != SPOTFLOW_OTA_PHASE_DOWNLOADING) {
		return -EINVAL;
	}
	main->status.phase = SPOTFLOW_OTA_PHASE_PENDING_UPGRADE;
	copy_state(main, out_state);
	return 0;
}

int spotflow_ota_main_fail_handler(struct ota_main_firmware_model* main,
				   struct spotflow_ota_main_firmware_state* out_state)
{
	if (main->presence != OTA_MAIN_FIRMWARE_PRESENT ||
	    (main->upgrade != OTA_MAIN_UPGRADE_HANDLER_ACTIVE &&
	     main->upgrade != OTA_MAIN_UPGRADE_COMMITTING) ||
	    main->probation.state != OTA_MAIN_PROBATION_NONE) {
		return -EINVAL;
	}
	spotflow_ota_main_finish_handler(main, SPOTFLOW_OTA_RESULT_FAILED);
	copy_state(main, out_state);
	return 0;
}

void spotflow_ota_main_finish_handler(struct ota_main_firmware_model* main,
				      enum spotflow_ota_result result)
{
	main->status.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING;
	main->status.is_paused = false;
	main->status.result = result;
	main->abort_requested = false;
	main->upgrade = OTA_MAIN_UPGRADE_IDLE;
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
	    main->presence != OTA_MAIN_FIRMWARE_PRESENT || main->artifact_index != artifact_index) {
		return -EINVAL;
	}
	spotflow_ota_main_finish_handler(main, result);
	main->probation.reconciled_result = result;
	main->probation.state = OTA_MAIN_PROBATION_COMPLETION_QUEUED;
	copy_state(main, out_state);
	return 0;
}

bool spotflow_ota_main_claim_completion(struct ota_main_firmware_model* main,
					struct ota_main_completion_transition* transition)
{
	if (main->probation.state != OTA_MAIN_PROBATION_COMPLETION_QUEUED) {
		return false;
	}
	*transition = (struct ota_main_completion_transition){
		.artifact_index = main->artifact_index,
		.artifact = main->artifact,
		.result = main->probation.reconciled_result,
		.claim_artifact_transaction = true,
	};
	main->probation.state = OTA_MAIN_PROBATION_COMPLETION_CLAIMED;
	return true;
}

int spotflow_ota_main_commit_probation_cleared(struct ota_main_firmware_model* main,
					       bool reconciliation_owned)
{
	if (!reconciliation_owned ||
	    main->probation.state != OTA_MAIN_PROBATION_COMPLETION_CLAIMED) {
		return -EINVAL;
	}
	main->probation.state = OTA_MAIN_PROBATION_NONE;
	main->upgrade = OTA_MAIN_UPGRADE_IDLE;
	return 0;
}

int spotflow_ota_main_finish_prereboot(struct ota_main_firmware_model* main, bool handler_owned,
				       struct ota_main_prereboot_transition* transition)
{
	if (!handler_owned || main->upgrade != OTA_MAIN_UPGRADE_COMMITTING ||
	    main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_UPGRADE) {
		return -EINVAL;
	}
	main->probation.state = OTA_MAIN_PROBATION_PENDING;
	main->upgrade = OTA_MAIN_UPGRADE_REBOOT_READY;
	main->status.phase = SPOTFLOW_OTA_PHASE_PENDING_REBOOT;
	transition->state = main->status;
	transition->clear_artifact_transaction = true;
	return 0;
}

int spotflow_ota_main_enter_unconfirmed(struct ota_main_firmware_model* main, size_t artifact_index,
					struct spotflow_ota_main_firmware_state* out_state)
{
	if (main->probation.state != OTA_MAIN_PROBATION_PENDING ||
	    main->presence != OTA_MAIN_FIRMWARE_PRESENT || main->artifact_index != artifact_index) {
		return -EINVAL;
	}
	main->status.phase = SPOTFLOW_OTA_PHASE_UNCONFIRMED;
	main->status.is_paused = false;
	main->status.result = SPOTFLOW_OTA_RESULT_PENDING;
	main->abort_requested = false;
	main->upgrade = OTA_MAIN_UPGRADE_IDLE;
	copy_state(main, out_state);
	return 0;
}

int spotflow_ota_main_set_paused(struct ota_main_firmware_model* main, bool has_attempt,
				 bool paused, struct spotflow_ota_main_firmware_state* out_state)
{
	if (!has_attempt ||
	    (paused &&
	     (!phase_allows_pause(main->status.phase) ||
	      main->upgrade == OTA_MAIN_UPGRADE_REBOOT_STARTED)) ||
	    (!paused && !main->status.is_paused)) {
		return -EINVAL;
	}
	main->status.is_paused = paused;
	copy_state(main, out_state);
	return 0;
}

int spotflow_ota_main_request_abort(struct ota_main_firmware_model* main, bool has_attempt,
				    struct spotflow_ota_main_firmware_state* out_state)
{
	if (!has_attempt || !phase_allows_abort(main->status.phase) ||
	    main->upgrade != OTA_MAIN_UPGRADE_HANDLER_ACTIVE) {
		copy_state(main, out_state);
		return -EINVAL;
	}
	main->abort_requested = true;
	main->status.is_paused = false;
	copy_state(main, out_state);
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
	if (!handler_owned || main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_UPGRADE ||
	    main->upgrade != OTA_MAIN_UPGRADE_HANDLER_ACTIVE) {
		return -EINVAL;
	}
	if (main->abort_requested || attempt_canceled) {
		return -ECANCELED;
	}
	if (main->status.is_paused) {
		return -EAGAIN;
	}
	main->upgrade = OTA_MAIN_UPGRADE_COMMITTING;
	return 0;
}

int spotflow_ota_main_cancel_upgrade_commit(struct ota_main_firmware_model* main)
{
	if (main->upgrade != OTA_MAIN_UPGRADE_COMMITTING ||
	    main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_UPGRADE) {
		return -EINVAL;
	}
	main->upgrade = OTA_MAIN_UPGRADE_HANDLER_ACTIVE;
	return 0;
}

int spotflow_ota_main_begin_reboot(struct ota_main_firmware_model* main)
{
	if (main->status.phase != SPOTFLOW_OTA_PHASE_PENDING_REBOOT ||
	    main->upgrade != OTA_MAIN_UPGRADE_REBOOT_READY ||
	    main->probation.state != OTA_MAIN_PROBATION_PENDING) {
		return -EINVAL;
	}
	if (main->status.is_paused) {
		return -EAGAIN;
	}
	main->upgrade = OTA_MAIN_UPGRADE_REBOOT_STARTED;
	return 0;
}

static bool phase_allows_pause(enum spotflow_ota_phase phase)
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

static bool phase_allows_abort(enum spotflow_ota_phase phase)
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

static void copy_state(const struct ota_main_firmware_model* main,
		       struct spotflow_ota_main_firmware_state* out_state)
{
	if (out_state != NULL) {
		*out_state = main->status;
	}
}
