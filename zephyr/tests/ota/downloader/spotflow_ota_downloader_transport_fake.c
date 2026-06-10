#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <spotflow/downloader.h>

#include "ota/spotflow_ota_downloader_transport.h"

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

		return 0;
	}

	if (offset >= fake->payload_len) {
		if (request->bytes_downloaded != NULL) {
			*request->bytes_downloaded = 0;
		}

		return 0;
	}

	size_t deliver_len = fake->payload_len - offset;

	if (fake->partial_transient_fail_after_bytes > offset) {
		deliver_len = MIN(deliver_len, fake->partial_transient_fail_after_bytes - offset);
	}

	struct spotflow_artifact_block block = {
		.offset = offset,
		.data = fake->payload + offset,
		.data_len = deliver_len,
		.is_last = offset + deliver_len == fake->payload_len,
	};

	request->callback(&block, request->downloader, request->callback_ctx);

	if (request->bytes_downloaded != NULL) {
		*request->bytes_downloaded = deliver_len;
	}

	if (fake->partial_transient_fail_after_bytes > 0 &&
	    offset + deliver_len < fake->payload_len &&
	    offset + deliver_len == fake->partial_transient_fail_after_bytes) {
		if (request->transient_failure != NULL) {
			*request->transient_failure = true;
		}

		return -EAGAIN;
	}

	return 0;
}

int spotflow_ota_downloader_transport_download(
    struct spotflow_ota_downloader_transport_request* request)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	int rc = 0;

	fake->call_count++;
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
		if (request->bytes_downloaded != NULL) {
			*request->bytes_downloaded = 0;
		}

		if (request->transient_failure != NULL) {
			*request->transient_failure = rc == -EAGAIN;
		}

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
