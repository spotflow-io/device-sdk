#include "ota/firmware/spotflow_ota_fw_main_common.h"
#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/firmware/spotflow_ota_psa.h"
#include "ota/platform/spotflow_ota_identity_stream.h"
#include "ota/persistence/spotflow_ota_persistence.h"

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

static K_MUTEX_DEFINE(psa_mutex);
static bool acceptance_pending_reboot;

static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx)
{
	int* write_error = callback_ctx;

	if (*write_error != 0) {
		return;
	}
	k_mutex_lock(&psa_mutex, K_FOREVER);
	*write_error = spotflow_ota_psa_write(block);
	k_mutex_unlock(&psa_mutex);
	if (*write_error != 0) {
		(void)spotflow_cancel_download(downloader);
	}
}

static void abort_download(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_abort();

	k_mutex_unlock(&psa_mutex);
	if (rc < 0) {
		LOG_ERR("Failed to clean PSA firmware download: %d", rc);
	}
}

enum spotflow_ota_result
spotflow_ota_fw_main_process_artifact(const struct spotflow_ota_worker_job* job)
{
	if (job == NULL || job->type != SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT ||
	    !job->data.process_artifact.artifact.is_main) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}
	const struct spotflow_ota_artifact* artifact = &job->data.process_artifact.artifact;
	const struct spotflow_ota_operation_token* token = &job->token;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_probation probation = {
		.attempt_id = token->attempt_id,
		.artifact_index = job->data.process_artifact.artifact_index,
	};
	psa_fwu_component_t component;
	int write_error = 0;
	int rc;

	if (artifact->url[0] == '\0' || artifact->secret[0] == '\0' ||
	    spotflow_ota_state_claim_main_firmware(job, &state) < 0) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}
	if (main_firmware_control_checkpoint() < 0) {
		return interrupted_main_firmware_result(token);
	}
	if (spotflow_ota_state_main_firmware_download_pending(token, &state) < 0) {
		return fail_main_firmware(token);
	}
	notify_main_firmware_state(&state);
	if (main_firmware_control_checkpoint() < 0) {
		return interrupted_main_firmware_result(token);
	}

	k_mutex_lock(&psa_mutex, K_FOREVER);
	rc = spotflow_ota_psa_begin();
	k_mutex_unlock(&psa_mutex);
	if (rc < 0) {
		goto failed;
	}
	struct spotflow_download_request request = {
		.url = artifact->url,
		.secret = artifact->secret,
	};

	rc = spotflow_ota_download_artifact(&main_firmware_downloader, &request, download_block_cb,
					    &write_error, download_started_cb, (void*)token);
	if (write_error != 0) {
		rc = write_error;
		goto failed;
	}
	if (rc < 0) {
		goto failed;
	}
	if (main_firmware_control_checkpoint() < 0) {
		rc = -ECANCELED;
		goto failed;
	}
	if (spotflow_ota_state_main_firmware_download_completed(token, &state) < 0) {
		rc = -EINVAL;
		goto failed;
	}
	notify_main_firmware_state(&state);
	if (main_firmware_control_checkpoint() < 0) {
		rc = -ECANCELED;
		goto failed;
	}

	k_mutex_lock(&psa_mutex, K_FOREVER);
	rc = spotflow_ota_psa_end(&component, probation.expected_build_id);
	/* Without new persistent container metadata, identical executable build IDs
	 * cannot distinguish an interrupted install from its already-running image.
	 */
	if (rc == 0 && spotflow_ota_identity_stream_matches(probation.expected_build_id)) {
		rc = -EALREADY;
	}
	if (rc == 0) {
		rc = spotflow_ota_psa_errno(psa_fwu_finish(component));
	}
	k_mutex_unlock(&psa_mutex);
	if (rc < 0) {
		goto failed;
	}
	strncpy(probation.slug, artifact->slug, sizeof(probation.slug) - 1);
	strncpy(probation.version, artifact->version, sizeof(probation.version) - 1);
	rc = begin_main_firmware_upgrade_commit(token);
	if (rc < 0) {
		goto failed;
	}
	rc = spotflow_ota_persistence_save_probation(&probation);
	if (rc < 0) {
		goto cancel_commit;
	}
	k_mutex_lock(&psa_mutex, K_FOREVER);
	rc = spotflow_ota_psa_check_install(component);
	if (rc == 0) {
		psa_status_t status = psa_fwu_install();

		/* Main-image installation must transfer execution through a reboot. */
		rc = status == PSA_SUCCESS_REBOOT
			? 0
			: (status < 0 ? spotflow_ota_psa_errno(status) : -ENOTSUP);
	}
	k_mutex_unlock(&psa_mutex);
	if (rc < 0) {
		goto cancel_commit;
	}
	rc = spotflow_ota_state_finish_main_firmware_prereboot(token, &state);
	if (rc < 0) {
		goto cancel_commit;
	}
	notify_main_firmware_state(&state);
	rc = begin_main_firmware_reboot(token);
	if (rc < 0) {
		/* Installation has been requested; preserve probation for recovery. */
		LOG_ERR("Failed to begin PSA installation reboot: %d", rc);
		return SPOTFLOW_OTA_RESULT_PENDING;
	}
	k_mutex_lock(&psa_mutex, K_FOREVER);
	rc = spotflow_ota_psa_errno(psa_fwu_request_reboot());
	k_mutex_unlock(&psa_mutex);
	if (rc < 0) {
		LOG_ERR("PSA installation reboot failed: %d; probation retained", rc);
	}
	/* A returning request (including an error) must not report success or
	 * discard the cross-reboot record. Startup resolves the actual outcome.
	 */
	return SPOTFLOW_OTA_RESULT_PENDING;

cancel_commit:
	(void)spotflow_ota_state_cancel_main_firmware_upgrade_commit(token);
failed:
	abort_download();
	if (rc == -ECANCELED) {
		return interrupted_main_firmware_result(token);
	}
	LOG_ERR("PSA main firmware update failed: %d", rc);
	return fail_main_firmware(token);
}

int spotflow_ota_fw_main_reconcile_startup(const struct spotflow_ota_probation* probation,
					   bool has_probation,
					   struct spotflow_ota_fw_main_startup_result* result)
{
	psa_fwu_component_info_t info;
	int rc;

	if (result == NULL || (has_probation && probation == NULL)) {
		return -EINVAL;
	}
	memset(result, 0, sizeof(*result));
	k_mutex_lock(&psa_mutex, K_FOREVER);
	acceptance_pending_reboot = false;
	rc = spotflow_ota_psa_init();
	if (rc == 0) {
		rc = spotflow_ota_psa_running_info(&info);
	}
	k_mutex_unlock(&psa_mutex);
	if (rc < 0 || !has_probation) {
		return rc;
	}
	if (!spotflow_ota_state_is_main_artifact_pending(probation->attempt_id,
							 probation->artifact_index)) {
		return spotflow_ota_persistence_clear_probation();
	}
	if (!spotflow_ota_identity_stream_matches(probation->expected_build_id)) {
		rc = spotflow_ota_state_queue_main_firmware_result(
			probation->attempt_id, probation->artifact_index,
			SPOTFLOW_OTA_RESULT_FAILED, &result->progress_state, &result->effects);
	} else if (info.state == PSA_FWU_TRIAL) {
		rc = spotflow_ota_state_enter_main_firmware_unconfirmed(
			probation->attempt_id, probation->artifact_index, &result->progress_state);
	} else if (info.state == PSA_FWU_UPDATED) {
		rc = spotflow_ota_state_queue_main_firmware_result(
			probation->attempt_id, probation->artifact_index,
			SPOTFLOW_OTA_RESULT_SUCCEEDED, &result->progress_state, &result->effects);
	} else if (info.state == PSA_FWU_FAILED || info.state == PSA_FWU_REJECTED) {
		rc = spotflow_ota_state_queue_main_firmware_result(
			probation->attempt_id, probation->artifact_index,
			SPOTFLOW_OTA_RESULT_FAILED, &result->progress_state, &result->effects);
	} else {
		/* An unexpected provider state is not evidence of commitment. */
		return -EIO;
	}
	result->has_progress_notification = rc == 0;
	return rc;
}

int spotflow_ota_fw_main_confirm_image(struct spotflow_ota_main_firmware_state* out_state,
				       spotflow_ota_state_effects* effects)
{
	struct spotflow_ota_main_firmware_view view;
	struct spotflow_ota_probation probation;
	psa_fwu_component_info_t info;
	bool has_probation;
	bool completed = false;
	int rc;

	if (effects == NULL) {
		return -EINVAL;
	}
	*effects = 0;
	k_mutex_lock(&psa_mutex, K_FOREVER);
	spotflow_ota_state_get_main_firmware_view(&view);
	if (!view.has_current_attempt) {
		rc = -EINVAL;
		goto done;
	}
	rc = spotflow_ota_psa_running_info(&info);
	if (rc < 0) {
		goto done;
	}
	if (view.state.phase == SPOTFLOW_OTA_PHASE_NOT_RUNNING &&
	    view.state.result == SPOTFLOW_OTA_RESULT_SUCCEEDED && info.state == PSA_FWU_UPDATED) {
		goto done;
	}
	rc = spotflow_ota_persistence_load_probation(&probation, &has_probation);
	if (rc < 0) {
		goto done;
	}
	if (!has_probation || probation.attempt_id != view.attempt_id ||
	    !spotflow_ota_state_is_main_artifact_pending(probation.attempt_id,
							 probation.artifact_index) ||
	    !spotflow_ota_identity_stream_matches(probation.expected_build_id)) {
		rc = -EINVAL;
		goto done;
	}
	if (info.state != PSA_FWU_UPDATED) {
		if (view.state.phase != SPOTFLOW_OTA_PHASE_UNCONFIRMED ||
		    info.state != PSA_FWU_TRIAL) {
			rc = -EINVAL;
			goto done;
		}
		if (acceptance_pending_reboot) {
			/* Repeated confirmation must preserve the SDK's pending request. */
			rc = 0;
			goto done;
		}
		rc = spotflow_ota_psa_check_accept();
		if (rc < 0) {
			goto done;
		}
		psa_status_t status = psa_fwu_accept();

		if (status == PSA_SUCCESS_REBOOT) {
			acceptance_pending_reboot = true;
			LOG_INF("PSA acceptance requested; application must request commit reboot");
			rc = 0;
			goto done;
		}
		rc = spotflow_ota_psa_errno(status);
		if (rc < 0) {
			goto done;
		}
		rc = spotflow_ota_psa_running_info(&info);
		if (rc < 0) {
			goto done;
		}
		if (info.state != PSA_FWU_UPDATED) {
			rc = -EIO;
			goto done;
		}
	}
	rc = spotflow_ota_state_queue_main_firmware_result(
		probation.attempt_id, probation.artifact_index, SPOTFLOW_OTA_RESULT_SUCCEEDED,
		&view.state, effects);
	completed = rc == 0;
done:
	if (out_state != NULL) {
		*out_state = view.state;
	}
	k_mutex_unlock(&psa_mutex);
	if (completed) {
		notify_main_firmware_state(&view.state);
	}
	return rc;
}
