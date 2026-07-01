#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <spotflow/downloader.h>

#include "ota/downloader/spotflow_ota_downloader_transport.h"

#include "spotflow_ota_downloader_transport_fake.h"

static struct spotflow_ota_downloader_transport_fake transport_fake;

struct spotflow_ota_downloader_transport_fake* spotflow_ota_downloader_transport_fake_get(void)
{
	return &transport_fake;
}

void spotflow_ota_downloader_transport_fake_reset(
    struct spotflow_ota_downloader_transport_fake* fake)
{
	memset(fake, 0, sizeof(*fake));
}

void spotflow_ota_downloader_transport_fake_set_results(
    struct spotflow_ota_downloader_transport_fake* fake, const int* results, size_t count)
{
	fake->next_result_count = 0;

	for (size_t i = 0; i < count && i < ARRAY_SIZE(fake->next_results); i++) {
		fake->next_results[i] = results[i];
		fake->next_result_count++;
	}

	fake->next_result_index = 0;
}

static int deliver_payload(struct spotflow_ota_downloader_transport_request* request,
			   struct spotflow_ota_downloader_transport_fake* fake, size_t offset)
{
	if (fake->payload == NULL || fake->payload_len == 0) {
		if (request->bytes_downloaded != NULL) {
			*request->bytes_downloaded = 0;
		}

		fake->bytes_delivered = 0;

		return 0;
	}

	if (offset >= fake->payload_len) {
		if (request->bytes_downloaded != NULL) {
			*request->bytes_downloaded = 0;
		}

		fake->bytes_delivered = 0;

		return 0;
	}

	const size_t chunk_size =
	    fake->chunk_size > 0 ? fake->chunk_size : fake->payload_len - offset;
	size_t delivered = 0;

	while (offset < fake->payload_len) {
		if (request->downloader->cancel_requested) {
			fake->cancel_observed = true;

			if (request->bytes_downloaded != NULL) {
				*request->bytes_downloaded = delivered;
			}

			fake->bytes_delivered = delivered;

			return -ECANCELED;
		}

		size_t deliver_len = fake->payload_len - offset;

		if (deliver_len > chunk_size) {
			deliver_len = chunk_size;
		}

		if (fake->partial_transient_fail_after_bytes > offset) {
			deliver_len =
			    MIN(deliver_len, fake->partial_transient_fail_after_bytes - offset);
		}

		struct spotflow_artifact_block block = {
			.offset = offset,
			.data = fake->payload + offset,
			.data_len = deliver_len,
			.is_last = offset + deliver_len == fake->payload_len,
		};

		request->callback(&block, request->downloader, request->callback_ctx);

		if (request->downloader->cancel_requested) {
			fake->cancel_observed = true;

			if (request->bytes_downloaded != NULL) {
				*request->bytes_downloaded = delivered + deliver_len;
			}

			fake->bytes_delivered = delivered + deliver_len;

			return -ECANCELED;
		}

		offset += deliver_len;
		delivered += deliver_len;

		if (fake->partial_transient_fail_after_bytes > 0 && offset < fake->payload_len &&
		    offset == fake->partial_transient_fail_after_bytes) {
			const int err =
			    fake->partial_fail_errno != 0 ? fake->partial_fail_errno : -EAGAIN;

			if (request->bytes_downloaded != NULL) {
				*request->bytes_downloaded = delivered;
			}

			fake->bytes_delivered = delivered;
			spotflow_ota_downloader_transport_note_error(request, delivered, err);

			return err;
		}
	}

	if (request->bytes_downloaded != NULL) {
		*request->bytes_downloaded = delivered;
	}

	fake->bytes_delivered = delivered;

	return 0;
}

int spotflow_ota_downloader_transport_download(
    struct spotflow_ota_downloader_transport_request* request)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	int rc = 0;

	fake->call_count++;
	*request->transient_failure = false;
	*request->bytes_downloaded = 0;

	strncpy(fake->last_authorization_header, request->authorization_header,
		sizeof(fake->last_authorization_header) - 1);
	strncpy(fake->last_host, request->url->host, sizeof(fake->last_host) - 1);
	strncpy(fake->last_path, request->url->path, sizeof(fake->last_path) - 1);
	fake->last_range_start = request->range_start;
	fake->tls = request->url->tls;
	fake->port = request->url->port;

	if (fake->next_result_index < fake->next_result_count) {
		rc = fake->next_results[fake->next_result_index++];
	}

	if (rc != 0) {
		spotflow_ota_downloader_transport_note_error(request, 0, rc);
		return rc;
	}

	if (fake->block_until_cancel) {
		while (!request->downloader->cancel_requested) {
			k_sleep(K_MSEC(10));
		}

		fake->cancel_observed = true;

		if (request->bytes_downloaded != NULL) {
			*request->bytes_downloaded = 0;
		}

		return -ECANCELED;
	}

	if (fake->block_until_pause) {
		while (request->downloader->state != SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
			if (request->downloader->cancel_requested) {
				fake->cancel_observed = true;

				if (request->bytes_downloaded != NULL) {
					*request->bytes_downloaded = 0;
				}

				return -ECANCELED;
			}

			k_sleep(K_MSEC(10));
		}

		fake->pause_observed = true;

		while (request->downloader->state == SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
			if (request->downloader->cancel_requested) {
				fake->cancel_observed = true;

				if (request->bytes_downloaded != NULL) {
					*request->bytes_downloaded = 0;
				}

				return -ECANCELED;
			}

			k_sleep(K_MSEC(10));
		}
	}

	return deliver_payload(request, fake, request->range_start);
}
