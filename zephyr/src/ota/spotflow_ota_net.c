#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "net/spotflow_mqtt.h"
#include "ota/spotflow_ota_cbor.h"
#include "ota/spotflow_ota_net.h"

#define SPOTFLOW_OTA_NET_MAX_CBOR_SIZE 128

struct pending_message_state {
	bool has_pending;
	struct spotflow_ota_cbor_update_results message;
	uint8_t buffer[SPOTFLOW_OTA_NET_MAX_CBOR_SIZE];
	size_t encoded_len;
};

static K_MUTEX_DEFINE(pending_message_mutex);
static struct pending_message_state pending_message;

static void clear_pending_message_locked(void);
static int encode_pending_message_locked(void);
static void remove_index(uint32_t* indexes, size_t* count, uint32_t index);
static int set_result_index(struct spotflow_ota_cbor_update_results* message, uint32_t index,
			    enum spotflow_ota_result result);

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

	if (!pending_message.has_pending || pending_message.message.attempt_id != attempt_id) {
		clear_pending_message_locked();
		pending_message.has_pending = true;
		pending_message.message.attempt_id = attempt_id;
	}

	if (pending_message.message.has_attempt_error) {
		k_mutex_unlock(&pending_message_mutex);
		return 0;
	}

	for (size_t i = 0; i < artifact_count; i++) {
		int rc =
		    set_result_index(&pending_message.message, (uint32_t)i, artifact_results[i]);

		if (rc < 0) {
			k_mutex_unlock(&pending_message_mutex);
			return rc;
		}
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
	pending_message.message.attempt_id = attempt_id;
	pending_message.message.has_attempt_error = true;
	pending_message.message.attempt_error = attempt_error;

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
}

static int encode_pending_message_locked(void)
{
	if (!pending_message.has_pending) {
		return 0;
	}

	return spotflow_ota_cbor_encode_update_results(
	    &pending_message.message, pending_message.buffer, sizeof(pending_message.buffer),
	    &pending_message.encoded_len);
}

static void remove_index(uint32_t* indexes, size_t* count, uint32_t index)
{
	for (size_t i = 0; i < *count; i++) {
		if (indexes[i] != index) {
			continue;
		}

		for (size_t j = i + 1; j < *count; j++) {
			indexes[j - 1] = indexes[j];
		}

		(*count)--;
		return;
	}
}

static int append_unique_index(uint32_t* indexes, size_t* count, uint32_t index)
{
	for (size_t i = 0; i < *count; i++) {
		if (indexes[i] == index) {
			return 0;
		}
	}

	if (*count >= CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	indexes[*count] = index;
	(*count)++;
	return 0;
}

static int set_result_index(struct spotflow_ota_cbor_update_results* message, uint32_t index,
			    enum spotflow_ota_result result)
{
	if (result == SPOTFLOW_OTA_RESULT_PENDING) {
		return 0;
	}

	remove_index(message->succeeded, &message->succeeded_count, index);
	remove_index(message->failed, &message->failed_count, index);
	remove_index(message->canceled, &message->canceled_count, index);

	switch (result) {
	case SPOTFLOW_OTA_RESULT_SUCCEEDED:
		return append_unique_index(message->succeeded, &message->succeeded_count, index);
	case SPOTFLOW_OTA_RESULT_FAILED:
		return append_unique_index(message->failed, &message->failed_count, index);
	case SPOTFLOW_OTA_RESULT_CANCELED:
		return append_unique_index(message->canceled, &message->canceled_count, index);
	default:
		return -EINVAL;
	}
}
