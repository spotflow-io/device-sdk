#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_cbor.h"
#include "ota/spotflow_ota_net.h"
#include "ota/spotflow_ota_persistence.h"
#include "ota/spotflow_ota_state.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static K_MUTEX_DEFINE(ota_mutex);
static bool ota_initialized;
static uint64_t last_received_attempt_id;

static void handle_ota_c2d_msg(uint8_t* payload, size_t len);
static void update_last_received_attempt_id(uint64_t attempt_id);
static int handle_decoded_c2d_message(const struct spotflow_ota_cbor_c2d_msg* msg);

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

	struct spotflow_ota_probation probation;
	bool has_probation;
	rc = spotflow_ota_persistence_load_probation(&probation, &has_probation);
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	/* TODO: Will be handled eventually */
	ARG_UNUSED(probation);
	ARG_UNUSED(has_probation);

	spotflow_ota_state_reset();
	last_received_attempt_id = has_attempt ? attempt.attempt_id : 0;
	ota_initialized = true;

	/* TODO: The attempt will be eventually propagated to the OTA state */

	k_mutex_unlock(&ota_mutex);
	return 0;
}

int spotflow_ota_init_session(void)
{
	int rc = spotflow_mqtt_request_ota_subscription(handle_ota_c2d_msg);
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

	if (!action.report_requested) {
		return 0;
	}

	spotflow_ota_state_get_snapshot(&snapshot);
	if (!snapshot.has_current_attempt || snapshot.current_attempt_id != msg->attempt_id ||
	    snapshot.has_attempt_error) {
		return 0;
	}

	return spotflow_ota_net_prepare_results(snapshot.current_attempt_id,
						snapshot.artifact_results, snapshot.artifact_count);
}
