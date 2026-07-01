#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota.h"
#include "ota/protocol/spotflow_ota_cbor.h"
#include "ota/firmware/spotflow_ota_fw_custom.h"
#include "ota/core/spotflow_ota_log.h"
#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
#include "ota/firmware/spotflow_ota_fw_main.h"
#endif
#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/core/spotflow_ota_state.h"
#include "ota/core/spotflow_ota_worker.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static K_MUTEX_DEFINE(ota_mutex);
static bool ota_initialized;
static uint64_t last_received_attempt_id;

static void handle_ota_c2d_msg(uint8_t* payload, size_t len);
static void update_last_received_attempt_id(uint64_t attempt_id);
static int handle_decoded_c2d_message(const struct spotflow_ota_cbor_c2d_msg* msg);
static void handle_state_action(const struct spotflow_ota_state_action* action);
static int prepare_persisted_results_for_attempt(uint64_t attempt_id);

int spotflow_ota_init(void)
{
	k_mutex_lock(&ota_mutex, K_FOREVER);

	if (ota_initialized) {
		k_mutex_unlock(&ota_mutex);
		return 0;
	}

	int rc = spotflow_ota_persistence_init();
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	struct spotflow_ota_persisted_attempt attempt;
	bool has_attempt;
	rc = spotflow_ota_persistence_load_attempt(&attempt, &has_attempt);
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	spotflow_ota_log_loaded_attempt(&attempt, has_attempt);

	struct spotflow_ota_probation probation;
	bool has_probation;
	rc = spotflow_ota_persistence_load_probation(&probation, &has_probation);
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	spotflow_ota_log_loaded_probation(&probation, has_probation);

	rc = spotflow_ota_worker_init();
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	spotflow_ota_state_init_from_persistence(has_attempt ? &attempt : NULL, has_attempt,
						 has_probation ? &probation : NULL, has_probation);

#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	{
		struct spotflow_ota_state_action action;

		rc = spotflow_ota_fw_main_reconcile_startup(has_probation ? &probation : NULL,
							    has_probation, &action);
		if (rc < 0) {
			k_mutex_unlock(&ota_mutex);
			return rc;
		}

		handle_state_action(&action);
	}
#endif

	last_received_attempt_id = has_attempt ? attempt.attempt_id : 0;
	ota_initialized = true;

	LOG_DBG("OTA initialized (last received attempt %llu)",
		(unsigned long long)last_received_attempt_id);

	k_mutex_unlock(&ota_mutex);
	return 0;
}

int spotflow_ota_init_session(void)
{
	int rc = spotflow_ota_init();
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_mqtt_request_ota_subscription(handle_ota_c2d_msg);
	if (rc < 0) {
		LOG_ERR("Failed to request subscription to OTA topic: %d", rc);
		return rc;
	}

	return 0;
}

int spotflow_ota_send_pending_message(void)
{
	return spotflow_ota_net_send_pending_message();
}

uint64_t spotflow_ota_get_last_received_attempt_id(void)
{
	uint64_t attempt_id;

	k_mutex_lock(&ota_mutex, K_FOREVER);
	attempt_id = last_received_attempt_id;
	k_mutex_unlock(&ota_mutex);

	return attempt_id;
}

void spotflow_ota_reset(void)
{
	k_mutex_lock(&ota_mutex, K_FOREVER);
	ota_initialized = false;
	last_received_attempt_id = 0;
	k_mutex_unlock(&ota_mutex);

	spotflow_ota_net_reset();
	spotflow_ota_state_reset();
	spotflow_ota_worker_reset();
#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	spotflow_ota_fw_main_reset();
#endif
}

static void handle_ota_c2d_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_cbor_c2d_msg msg;
	struct spotflow_ota_cbor_decode_status status;
	int rc = spotflow_ota_cbor_decode_c2d(payload, len, &msg, &status);

	if (status.has_trustworthy_attempt_id) {
		update_last_received_attempt_id(status.attempt_id);
	}

	if (rc < 0) {
		if (status.has_trustworthy_attempt_id && status.has_attempt_error) {
			struct spotflow_ota_state_action action;

			rc = spotflow_ota_state_reject_update(status.attempt_id,
							      status.attempt_error, &action);
			if (rc < 0) {
				LOG_ERR("Failed to reject OTA attempt %llu: %d",
					(unsigned long long)status.attempt_id, rc);
				return;
			}

			handle_state_action(&action);
		}

		return;
	}

	rc = handle_decoded_c2d_message(&msg);
	if (rc < 0) {
		LOG_ERR("Failed to handle OTA C2D message for attempt %llu: %d",
			(unsigned long long)msg.attempt_id, rc);
	}
}

static void update_last_received_attempt_id(uint64_t attempt_id)
{
	k_mutex_lock(&ota_mutex, K_FOREVER);
	if (attempt_id > last_received_attempt_id) {
		last_received_attempt_id = attempt_id;
	}
	k_mutex_unlock(&ota_mutex);
}

static int handle_decoded_c2d_message(const struct spotflow_ota_cbor_c2d_msg* msg)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	int rc;

	switch (msg->type) {
	case SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS:
		rc = spotflow_ota_state_accept_update(&msg->payload.update, &action);
		break;
	case SPOTFLOW_OTA_CBOR_MSG_CANCEL_UPDATE:
		rc = spotflow_ota_state_accept_cancel(msg->attempt_id, &action);
		break;
	case SPOTFLOW_OTA_CBOR_MSG_REPORT_UPDATE_RESULTS:
		rc = spotflow_ota_state_accept_report_request(msg->attempt_id, &action);
		break;
	default:
		return -EINVAL;
	}

	if (rc < 0) {
		return rc;
	}

	if (msg->type == SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS) {
		if (action.accepted_update) {
			LOG_INF("OTA attempt %llu accepted (%zu artifacts)",
				(unsigned long long)msg->payload.update.attempt_id,
				msg->payload.update.artifact_count);
		} else if (action.ignored_duplicate_update) {
			LOG_INF("Ignoring duplicate UPDATE_ARTIFACTS for OTA attempt %llu",
				(unsigned long long)msg->payload.update.attempt_id);
		} else if (action.superseded_current) {
			spotflow_ota_state_get_snapshot(&snapshot);
			LOG_DBG("OTA attempt %llu superseded; pending attempt %llu",
				(unsigned long long)snapshot.current_attempt_id,
				(unsigned long long)snapshot.pending_attempt_id);
		}
	} else if (msg->type == SPOTFLOW_OTA_CBOR_MSG_CANCEL_UPDATE) {
		if (action.accepted_cancel) {
			LOG_INF("OTA attempt %llu canceled", (unsigned long long)msg->attempt_id);
		} else if (action.ignored_late_cancel) {
			LOG_DBG("Ignoring late CANCEL_UPDATE for OTA attempt %llu",
				(unsigned long long)msg->attempt_id);
		}
	}

	handle_state_action(&action);

	if (!action.report_requested) {
		return 0;
	}

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt || snapshot.current_attempt_id != msg->attempt_id) {
		return prepare_persisted_results_for_attempt(msg->attempt_id);
	}

	if (snapshot.has_attempt_error) {
		return prepare_persisted_results_for_attempt(msg->attempt_id);
	}

	return spotflow_ota_net_prepare_results(snapshot.current_attempt_id,
						snapshot.artifact_results, snapshot.artifact_count);
}

bool spotflow_is_update_canceled(void)
{
	if (spotflow_ota_init() < 0) {
		return false;
	}

	return spotflow_ota_state_is_update_canceled();
}

int spotflow_get_main_firmware_update_state(struct spotflow_ota_main_firmware_state* state)
{
	if (state == NULL) {
		return -EINVAL;
	}

	int rc = spotflow_ota_init();
	if (rc < 0) {
		return rc;
	}

	struct spotflow_ota_state_snapshot snapshot;
	spotflow_ota_state_get_snapshot(&snapshot);
	*state = snapshot.main_firmware_state;
	return 0;
}

int spotflow_get_main_firmware_update_info(struct spotflow_firmware_info* info,
					   struct spotflow_download_request* request)
{
	if (info == NULL || request == NULL) {
		return -EINVAL;
	}

	int rc = spotflow_ota_init();
	if (rc < 0) {
		return rc;
	}

	return spotflow_ota_state_get_main_firmware_info(info, request);
}

int spotflow_pause_main_firmware_update(struct spotflow_ota_main_firmware_state* state)
{
#if !IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (state != NULL) {
		(void)spotflow_get_main_firmware_update_state(state);
	}

	return -ENOTSUP;
#else
	int rc;

	rc = spotflow_ota_init();
	if (rc < 0) {
		return rc;
	}

	return spotflow_ota_fw_main_pause_update(state);
#endif
}

int spotflow_resume_main_firmware_update(struct spotflow_ota_main_firmware_state* state)
{
#if !IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (state != NULL) {
		(void)spotflow_get_main_firmware_update_state(state);
	}

	return -ENOTSUP;
#else
	int rc;

	rc = spotflow_ota_init();
	if (rc < 0) {
		return rc;
	}

	return spotflow_ota_fw_main_resume_update(state);
#endif
}

int spotflow_abort_main_firmware_update(struct spotflow_ota_main_firmware_state* state)
{
#if !IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (state != NULL) {
		(void)spotflow_get_main_firmware_update_state(state);
	}

	return -ENOTSUP;
#else
	struct spotflow_ota_state_action action;
	int rc;

	rc = spotflow_ota_init();
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_ota_fw_main_fail_update(state, &action);
	if (rc < 0) {
		return rc;
	}

	handle_state_action(&action);
	return 0;
#endif
}

int spotflow_confirm_main_firmware_image(struct spotflow_ota_main_firmware_state* state)
{
#if !IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (state != NULL) {
		(void)spotflow_get_main_firmware_update_state(state);
	}

	return -ENOTSUP;
#else
	struct spotflow_ota_state_action action;
	int rc;

	rc = spotflow_ota_init();
	if (rc < 0) {
		LOG_ERR("Failed to initialize OTA before main firmware confirmation: %d", rc);
		return rc;
	}

	rc = spotflow_ota_fw_main_confirm_image(state, &action);
	if (rc < 0) {
		return rc;
	}

	handle_state_action(&action);
	return 0;
#endif
}

static void handle_state_action(const struct spotflow_ota_state_action* action)
{
	if (action == NULL) {
		return;
	}

	if (action->accepted_cancel) {
		spotflow_ota_fw_custom_notify_canceled();
#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
		spotflow_ota_fw_main_cancel_active_download();
#endif
	}

#if IS_ENABLED(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE)
	if (action->superseded_current) {
		spotflow_ota_fw_main_cancel_active_download();
	}
#endif

	if (action->wake_worker) {
		spotflow_ota_worker_wake();
	}
}

static int prepare_persisted_results_for_attempt(uint64_t attempt_id)
{
	struct spotflow_ota_persisted_attempt attempt;
	bool has_attempt;
	int rc = spotflow_ota_persistence_load_attempt(&attempt, &has_attempt);
	if (rc < 0) {
		return rc;
	}

	if (!has_attempt || attempt.attempt_id != attempt_id) {
		return 0;
	}

	if (attempt.has_attempt_error) {
		return spotflow_ota_net_prepare_attempt_error(attempt.attempt_id,
							      attempt.attempt_error);
	}

	return spotflow_ota_net_prepare_results(attempt.attempt_id, attempt.artifact_results,
						attempt.artifact_count);
}
