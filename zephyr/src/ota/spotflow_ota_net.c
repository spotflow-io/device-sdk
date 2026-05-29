#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "net/spotflow_mqtt.h"
#include "ota/spotflow_ota_cbor.h"
#include "ota/spotflow_ota_net.h"

#define SPOTFLOW_OTA_NET_MAX_CBOR_SIZE 128

struct pending_message_state {
	bool has_pending;
	uint64_t attempt_id;
	bool has_attempt_error;
	enum spotflow_ota_attempt_error attempt_error;
	size_t artifact_count;
	enum spotflow_ota_result artifact_results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
	uint8_t buffer[SPOTFLOW_OTA_NET_MAX_CBOR_SIZE];
	size_t encoded_len;
};

static K_MUTEX_DEFINE(pending_message_mutex);
static struct pending_message_state pending_message;

static void clear_pending_message_locked(void);
static int encode_pending_message_locked(void);

void spotflow_ota_net_reset(void)
{
	k_mutex_lock(&pending_message_mutex, K_FOREVER);
	clear_pending_message_locked();
	k_mutex_unlock(&pending_message_mutex);
}

int spotflow_ota_net_prepare_results(uint64_t attempt_id,
				     const enum spotflow_ota_result* artifact_results,
				     size_t artifact_count)
{
	if (attempt_id == 0 || (artifact_results == NULL && artifact_count > 0) ||
	    artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	k_mutex_lock(&pending_message_mutex, K_FOREVER);

	if (!pending_message.has_pending || pending_message.attempt_id != attempt_id) {
		clear_pending_message_locked();
		pending_message.has_pending = true;
		pending_message.attempt_id = attempt_id;
		pending_message.artifact_count = artifact_count;
	}

	if (pending_message.has_attempt_error) {
		k_mutex_unlock(&pending_message_mutex);
		return 0;
	}

	if (artifact_count > pending_message.artifact_count) {
		pending_message.artifact_count = artifact_count;
	}

	for (size_t i = 0; i < artifact_count; i++) {
		if (artifact_results[i] == SPOTFLOW_OTA_RESULT_PENDING) {
			continue;
		}

		pending_message.artifact_results[i] = artifact_results[i];
	}

	int rc = encode_pending_message_locked();

	k_mutex_unlock(&pending_message_mutex);
	return rc;
}

int spotflow_ota_net_prepare_attempt_error(uint64_t attempt_id,
					   enum spotflow_ota_attempt_error attempt_error)
{
	if (attempt_id == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&pending_message_mutex, K_FOREVER);
	clear_pending_message_locked();
	pending_message.has_pending = true;
	pending_message.attempt_id = attempt_id;
	pending_message.has_attempt_error = true;
	pending_message.attempt_error = attempt_error;

	int rc = encode_pending_message_locked();

	k_mutex_unlock(&pending_message_mutex);
	return rc;
}

int spotflow_ota_net_send_pending_message(void)
{
	k_mutex_lock(&pending_message_mutex, K_FOREVER);

	if (!pending_message.has_pending) {
		k_mutex_unlock(&pending_message_mutex);
		return 0;
	}

	int rc =
	    spotflow_mqtt_publish_ota_cbor_msg(pending_message.buffer, pending_message.encoded_len);

	if (rc == 0) {
		clear_pending_message_locked();
	}

	k_mutex_unlock(&pending_message_mutex);
	return rc;
}

static void clear_pending_message_locked(void)
{
	memset(&pending_message, 0, sizeof(pending_message));
	for (size_t i = 0; i < ARRAY_SIZE(pending_message.artifact_results); i++) {
		pending_message.artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}
}

static int encode_pending_message_locked(void)
{
	if (!pending_message.has_pending) {
		return 0;
	}

	struct spotflow_ota_cbor_update_results message = {
		.attempt_id = pending_message.attempt_id,
		.has_attempt_error = pending_message.has_attempt_error,
		.attempt_error = pending_message.attempt_error,
	};

	if (!pending_message.has_attempt_error) {
		for (size_t i = 0; i < pending_message.artifact_count; i++) {
			switch (pending_message.artifact_results[i]) {
			case SPOTFLOW_OTA_RESULT_PENDING:
				break;
			case SPOTFLOW_OTA_RESULT_SUCCEEDED:
				message.succeeded[message.succeeded_count++] = (uint32_t)i;
				break;
			case SPOTFLOW_OTA_RESULT_FAILED:
				message.failed[message.failed_count++] = (uint32_t)i;
				break;
			case SPOTFLOW_OTA_RESULT_CANCELED:
				message.canceled[message.canceled_count++] = (uint32_t)i;
				break;
			default:
				return -EINVAL;
			}
		}
	}

	return spotflow_ota_cbor_encode_update_results(&message, pending_message.buffer,
						       sizeof(pending_message.buffer),
						       &pending_message.encoded_len);
}
