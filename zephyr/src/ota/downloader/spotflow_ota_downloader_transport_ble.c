#include "ota/downloader/spotflow_ota_downloader_transport.h"

#include "net/transport/ble/spotflow_ble_transport.h"
#include "net/transport/ble/spotflow_ble_transport_internal.h"
#include "ota/downloader/spotflow_ota_downloader.h"
#include "ota/protocol/spotflow_ota_ble_protocol.h"

#include <errno.h>
#include <string.h>

#include <zephyr/random/random.h>

#define TRANSFER_RETRY_INTERVAL_MS 2000
#define TRANSFER_STATE_POLL_MS 100

BUILD_ASSERT(CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE <=
	     UINT16_MAX - SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE);

struct spotflow_ota_ble_response_state {
	struct spotflow_ota_ble_header expected;
	bool accepting;
	size_t payload_len;
	uint8_t payload[CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE +
			SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE];
};

/* One outstanding block bounds RAM and makes the next READ acknowledge the
 * previous callback's consumption. Multiple downloader instances share the link.
 */
static K_MUTEX_DEFINE(transfer_lock);
/* Owned by transfer_lock for the entire OPEN/READ/CLOSE attempt. A reconnect
 * between exchanges must invalidate the transfer as well as an outstanding wait.
 */
static uint32_t transfer_generation;
static K_MUTEX_DEFINE(response_lock);
static K_SEM_DEFINE(response_ready, 0, 1);
static struct spotflow_ota_ble_response_state response_state;

static int acquire_transfer(struct spotflow_ota_downloader_transport_request* request);
static bool transfer_is_connected(void);
static int send_request(uint8_t* message, size_t len);
static int exchange(struct spotflow_ota_downloader_transport_request* request, uint8_t* message,
		    size_t len, struct spotflow_ota_ble_response* response);
static int wait_for_response(struct spotflow_ota_downloader_transport_request* request,
			     uint8_t* message, size_t len);
static int open_transfer(struct spotflow_ota_downloader_transport_request* request,
			 struct spotflow_ota_ble_header* header, uint32_t* artifact_size);
static int consume_block(struct spotflow_ota_downloader_transport_request* request,
			 struct spotflow_ota_ble_header* header, uint32_t offset, uint32_t total);
static int close_transfer(struct spotflow_ota_downloader_transport_request* request,
			  struct spotflow_ota_ble_header* header,
			  enum spotflow_ota_ble_close_reason reason);

void spotflow_ble_artifact_receive(const uint8_t* payload, size_t len)
{
	struct spotflow_ota_ble_header header;

	if (payload == NULL || len < SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE ||
	    len > sizeof(response_state.payload) ||
	    spotflow_ota_ble_decode_header(payload, len, &header) != 0) {
		return;
	}

	k_mutex_lock(&response_lock, K_FOREVER);
	if (response_state.accepting && response_state.payload_len == 0 &&
	    header.operation == response_state.expected.operation &&
	    header.transfer_id == response_state.expected.transfer_id &&
	    header.request_id == response_state.expected.request_id) {
		memcpy(response_state.payload, payload, len);
		response_state.payload_len = len;
		k_sem_give(&response_ready);
	}
	k_mutex_unlock(&response_lock);
}

int spotflow_ota_downloader_transport_download(
	struct spotflow_ota_downloader_transport_request* request)
{
	if (request == NULL || request->original_url == NULL || request->secret == NULL ||
	    request->downloader == NULL || request->callback == NULL ||
	    request->bytes_downloaded == NULL || request->artifact_size == NULL ||
	    request->paused == NULL || request->transient_failure == NULL) {
		return -EINVAL;
	}

	*request->bytes_downloaded = 0;
	*request->paused = false;
	*request->transient_failure = false;

	int rc = acquire_transfer(request);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	transfer_generation = g_spotflow_ble_transport_state.tx.connection_generation;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	struct spotflow_ota_ble_header header = {
		.transfer_id = sys_rand32_get() | 1U,
		.request_id = 1,
	};
	uint32_t artifact_size;

	rc = open_transfer(request, &header, &artifact_size);
	if (rc == 0) {
		uint32_t offset = request->range_start;

		while (offset < artifact_size) {
			rc = consume_block(request, &header, offset, artifact_size);
			if (rc != 0) {
				break;
			}
			offset = request->range_start + *request->bytes_downloaded;
		}
	}

	if (rc == 0) {
		/* CLOSE acknowledges the final callback's consumption. A retry at the
		 * artifact size closes without invoking that callback a second time.
		 */
		rc = close_transfer(request, &header, SPOTFLOW_OTA_BLE_CLOSE_CONSUMED);
	}
	if (rc != 0 && transfer_is_connected()) {
		(void)close_transfer(request, &header, SPOTFLOW_OTA_BLE_CLOSE_ABORTED);
	}

	k_mutex_unlock(&transfer_lock);
	if (!*request->paused) {
		spotflow_ota_downloader_transport_note_error(request, *request->bytes_downloaded,
							     rc);
	}
	return rc;
}

static int acquire_transfer(struct spotflow_ota_downloader_transport_request* request)
{
	while (k_mutex_lock(&transfer_lock, K_MSEC(TRANSFER_STATE_POLL_MS)) != 0) {
		int rc = spotflow_ota_downloader_check_state(request->downloader);

		if (rc != 0) {
			*request->paused = rc == -EAGAIN;
			return rc;
		}
	}
	return 0;
}

static int send_request(uint8_t* message, size_t len)
{
	return spotflow_ble_transport_send_framed_message_on_connection(
		SPOTFLOW_MSG_ARTIFACT_REQUEST, &g_spotflow_ble_transport_state.tx.artifact_sequence,
		message, len, transfer_generation);
}

static bool transfer_is_connected(void)
{
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	bool connected = g_spotflow_ble_transport_state.tx.conn != NULL &&
		g_spotflow_ble_transport_state.tx.notifications_enabled &&
		g_spotflow_ble_transport_state.tx.connection_generation == transfer_generation;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	return connected;
}

static int exchange(struct spotflow_ota_downloader_transport_request* request, uint8_t* message,
		    size_t len, struct spotflow_ota_ble_response* response)
{
	struct spotflow_ota_ble_header header;
	int rc = spotflow_ota_ble_decode_header(message, len, &header);

	if (rc != 0) {
		return rc;
	}
	header.operation |= SPOTFLOW_OTA_BLE_RESPONSE_FLAG;

	k_mutex_lock(&response_lock, K_FOREVER);
	response_state.expected = header;
	response_state.accepting = true;
	response_state.payload_len = 0;
	k_sem_reset(&response_ready);
	k_mutex_unlock(&response_lock);

	rc = wait_for_response(request, message, len);

	/* Freeze the response before exposing borrowed data to the downloader.
	 * transfer_lock prevents another downloader from starting an exchange.
	 */
	k_mutex_lock(&response_lock, K_FOREVER);
	response_state.accepting = false;
	k_mutex_unlock(&response_lock);

	if (rc == 0) {
		rc = spotflow_ota_ble_decode_response(response_state.payload,
						      response_state.payload_len, response);
	}
	return rc == 0 ? spotflow_ota_ble_status_error(response->status) : rc;
}

static int wait_for_response(struct spotflow_ota_downloader_transport_request* request,
			     uint8_t* message, size_t len)
{
	int64_t deadline = k_uptime_get() + CONFIG_SPOTFLOW_OTA_BLE_REQUEST_TIMEOUT_MS;
	int64_t next_send = 0;

	while (true) {
		int rc = spotflow_ota_downloader_check_state(request->downloader);

		if (rc != 0) {
			*request->paused = rc == -EAGAIN;
			return rc;
		}
		if (!transfer_is_connected()) {
			return -ENOTCONN;
		}
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}

		if (k_uptime_get() >= next_send) {
			rc = send_request(message, len);
			if (rc != 0) {
				return rc;
			}
			next_send = k_uptime_get() + TRANSFER_RETRY_INTERVAL_MS;
		}
		if (k_sem_take(&response_ready, K_MSEC(TRANSFER_STATE_POLL_MS)) == 0) {
			return transfer_is_connected() ? 0 : -ENOTCONN;
		}
	}
}

static int open_transfer(struct spotflow_ota_downloader_transport_request* request,
			 struct spotflow_ota_ble_header* header, uint32_t* artifact_size)
{
	uint8_t message[SPOTFLOW_OTA_BLE_OPEN_MAX_SIZE];
	int message_len = spotflow_ota_ble_encode_open(message, sizeof(message), header,
						       request->original_url, request->secret);
	struct spotflow_ota_ble_response response;

	header->request_id++;
	int rc = message_len < 0 ? message_len : exchange(request, message, message_len, &response);

	/* URL and secret are only sent to the connected gateway, never to logs. */
	memset(message, 0, sizeof(message));
	if (rc != 0) {
		return rc;
	}
	if (response.position == 0 || request->range_start > response.position ||
	    (*request->artifact_size != 0 && *request->artifact_size != response.position)) {
		return -EPROTO;
	}

	*artifact_size = response.position;
	*request->artifact_size = response.position;
	return 0;
}

static int consume_block(struct spotflow_ota_downloader_transport_request* request,
			 struct spotflow_ota_ble_header* header, uint32_t offset, uint32_t total)
{
	uint8_t message[SPOTFLOW_OTA_BLE_READ_SIZE];
	size_t count = MIN(sizeof(request->downloader->recv_buf), total - offset);
	int message_len =
		spotflow_ota_ble_encode_read(message, sizeof(message), header, offset, count);
	struct spotflow_ota_ble_response response;

	header->request_id++;
	int rc = message_len < 0 ? message_len : exchange(request, message, message_len, &response);

	if (rc != 0) {
		return rc;
	}
	if (response.position != offset || response.data_len != count) {
		return -EPROTO;
	}

	memcpy(request->downloader->recv_buf, response.data, count);
	struct spotflow_artifact_block block = {
		.offset = offset,
		.data = request->downloader->recv_buf,
		.data_len = count,
		.is_last = offset + count == total,
	};

	request->callback(&block, request->downloader, request->callback_ctx);
	*request->bytes_downloaded += count;
	return 0;
}

static int close_transfer(struct spotflow_ota_downloader_transport_request* request,
			  struct spotflow_ota_ble_header* header,
			  enum spotflow_ota_ble_close_reason reason)
{
	uint8_t message[SPOTFLOW_OTA_BLE_CLOSE_SIZE];
	uint32_t consumed = request->range_start + *request->bytes_downloaded;
	int message_len =
		spotflow_ota_ble_encode_close(message, sizeof(message), header, consumed, reason);
	struct spotflow_ota_ble_response response;

	header->request_id++;
	if (message_len < 0) {
		return message_len;
	}
	/* Abort is best effort: cancellation must not wait for a gateway response. */
	return reason == SPOTFLOW_OTA_BLE_CLOSE_ABORTED
		? send_request(message, message_len)
		: exchange(request, message, message_len, &response);
}
