#include "ota/downloader/spotflow_ota_downloader_transport.h"

#include "net/transport/ble/spotflow_ble_transport.h"
#include "net/transport/ble/spotflow_ble_transport_internal.h"
#include "ota/downloader/spotflow_ota_downloader.h"

#include <errno.h>
#include <string.h>

#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#define TRANSFER_VERSION 1
#define TRANSFER_OPEN 1
#define TRANSFER_READ 2
#define TRANSFER_CLOSE 3
#define TRANSFER_HEADER_SIZE 10
#define TRANSFER_RESPONSE_HEADER_SIZE 11
#define TRANSFER_DATA_HEADER_SIZE 15
#define TRANSFER_RETRY_INTERVAL_MS 2000

BUILD_ASSERT(CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE <= UINT16_MAX - TRANSFER_DATA_HEADER_SIZE);

/* One outstanding block bounds RAM and makes the next READ acknowledge the
 * previous callback's consumption. Multiple downloader instances share the link.
 */
static K_MUTEX_DEFINE(transfer_lock);
static K_MUTEX_DEFINE(response_lock);
static K_SEM_DEFINE(response_ready, 0, 1);
static uint32_t expected_transfer;
static uint32_t expected_request;
static uint8_t expected_operation;
static size_t response_len;
static uint8_t response[CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE + TRANSFER_DATA_HEADER_SIZE];

void spotflow_ble_artifact_receive(const uint8_t* payload, size_t len)
{
	if (len < TRANSFER_RESPONSE_HEADER_SIZE || len > sizeof(response) ||
	    payload[0] != TRANSFER_VERSION) {
		return;
	}
	k_mutex_lock(&response_lock, K_FOREVER);
	if (expected_transfer != 0 && response_len == 0 && payload[1] == expected_operation &&
	    sys_get_le32(payload + 2) == expected_transfer &&
	    sys_get_le32(payload + 6) == expected_request) {
		memcpy(response, payload, len);
		response_len = len;
		k_sem_give(&response_ready);
	}
	k_mutex_unlock(&response_lock);
}

static int send_request(uint8_t* message, size_t len)
{
	return spotflow_ble_transport_send_framed_message(
		SPOTFLOW_MSG_ARTIFACT_REQUEST, &g_spotflow_ble_transport_state.tx.artifact_sequence,
		message, len);
}

static int response_error(uint8_t status)
{
	switch (status) {
	case 0:
		return 0;
	case 2:
		return -EACCES;
	case 3:
		return -EAGAIN;
	case 4:
		return -EFBIG;
	default:
		return -EPROTO;
	}
}

static void encode_header(uint8_t* message, uint8_t operation, uint32_t transfer, uint32_t request)
{
	message[0] = TRANSFER_VERSION;
	message[1] = operation;
	sys_put_le32(transfer, message + 2);
	sys_put_le32(request, message + 6);
}

static int exchange(struct spotflow_ota_downloader_transport_request* request, uint8_t* message,
		    size_t len)
{
	k_mutex_lock(&response_lock, K_FOREVER);
	expected_transfer = sys_get_le32(message + 2);
	expected_request = sys_get_le32(message + 6);
	expected_operation = message[1] | 0x80;
	response_len = 0;
	k_sem_reset(&response_ready);
	k_mutex_unlock(&response_lock);

	int64_t deadline = k_uptime_get() + CONFIG_SPOTFLOW_OTA_HTTP_TIMEOUT_MS;
	int64_t next_send = 0;
	int rc;
	while (true) {
		rc = spotflow_ota_downloader_check_state(request->downloader);
		if (rc != 0) {
			*request->paused = rc == -EAGAIN;
			break;
		}
		if (!spotflow_ble_transport_is_ready()) {
			rc = -ENOTCONN;
			break;
		}
		if (k_uptime_get() >= deadline) {
			rc = -ETIMEDOUT;
			break;
		}
		if (k_uptime_get() >= next_send) {
			rc = send_request(message, len);
			if (rc != 0) {
				break;
			}
			next_send = k_uptime_get() + TRANSFER_RETRY_INTERVAL_MS;
		}
		if (k_sem_take(&response_ready, K_MSEC(100)) == 0) {
			k_mutex_lock(&response_lock, K_FOREVER);
			rc = response_error(response[10]);
			k_mutex_unlock(&response_lock);
			break;
		}
	}

	/* Stop accepting duplicates before the caller inspects the response. */
	k_mutex_lock(&response_lock, K_FOREVER);
	expected_transfer = 0;
	k_mutex_unlock(&response_lock);
	return rc;
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
	size_t url_len = strlen(request->original_url);
	size_t secret_len = strlen(request->secret);
	if (url_len > SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH ||
	    secret_len > SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH) {
		return -EMSGSIZE;
	}

	int rc;
	while (k_mutex_lock(&transfer_lock, K_MSEC(100)) != 0) {
		rc = spotflow_ota_downloader_check_state(request->downloader);
		if (rc != 0) {
			*request->paused = rc == -EAGAIN;
			return rc;
		}
	}
	uint32_t transfer = sys_rand32_get() | 1U;
	uint32_t request_id = 1;
	uint8_t open[TRANSFER_HEADER_SIZE + 3 + SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH +
		     SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH];
	encode_header(open, TRANSFER_OPEN, transfer, request_id++);
	sys_put_le16(url_len, open + 10);
	open[12] = (uint8_t)secret_len;
	memcpy(open + 13, request->original_url, url_len);
	memcpy(open + 13 + url_len, request->secret, secret_len);
	rc = exchange(request, open, 13 + url_len + secret_len);
	/* URL and secret are only sent to the connected gateway, never to logs. */
	memset(open, 0, sizeof(open));
	if (rc != 0) {
		goto finish;
	}
	if (response_len != TRANSFER_DATA_HEADER_SIZE) {
		rc = -EPROTO;
		goto finish;
	}
	uint32_t total = sys_get_le32(response + 11);
	if (total == 0 || request->range_start > total ||
	    (*request->artifact_size != 0 && *request->artifact_size != total)) {
		rc = -EPROTO;
		goto finish;
	}
	*request->artifact_size = total;
	size_t offset = request->range_start;
	while (offset < total) {
		uint8_t read[16];
		size_t count = MIN(sizeof(request->downloader->recv_buf), total - offset);
		encode_header(read, TRANSFER_READ, transfer, request_id++);
		sys_put_le32(offset, read + 10);
		sys_put_le16(count, read + 14);
		rc = exchange(request, read, sizeof(read));
		if (rc != 0) {
			goto finish;
		}
		if (response_len != TRANSFER_DATA_HEADER_SIZE + count ||
		    sys_get_le32(response + 11) != offset) {
			rc = -EPROTO;
			goto finish;
		}
		memcpy(request->downloader->recv_buf, response + TRANSFER_DATA_HEADER_SIZE, count);
		struct spotflow_artifact_block block = {
			.offset = offset,
			.data = request->downloader->recv_buf,
			.data_len = count,
			.is_last = offset + count == total,
		};
		request->callback(&block, request->downloader, request->callback_ctx);
		offset += count;
		*request->bytes_downloaded = offset - request->range_start;
	}

	/* CLOSE explicitly acknowledges consumption of the final block. If its reply
	 * is lost, a retry at total length closes without invoking the callback again.
	 */
	uint8_t close[15];
	encode_header(close, TRANSFER_CLOSE, transfer, request_id++);
	sys_put_le32(total, close + 10);
	close[14] = 0;
	rc = exchange(request, close, sizeof(close));
	if (rc == 0 && response_len != TRANSFER_RESPONSE_HEADER_SIZE) {
		rc = -EPROTO;
	}

finish:
	if (rc != 0 && spotflow_ble_transport_is_ready()) {
		uint8_t abort[15];
		encode_header(abort, TRANSFER_CLOSE, transfer, request_id);
		sys_put_le32(request->range_start + *request->bytes_downloaded, abort + 10);
		abort[14] = 1;
		(void)send_request(abort, sizeof(abort));
	}
	k_mutex_unlock(&transfer_lock);
	if (!*request->paused) {
		spotflow_ota_downloader_transport_note_error(request, *request->bytes_downloaded,
							     rc);
	}
	return rc;
}
