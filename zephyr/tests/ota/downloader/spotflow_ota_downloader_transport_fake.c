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
	fake->tls = request->url->tls;
	fake->port = request->url->port;

	if (fake->next_result_index < fake->next_result_count) {
		rc = fake->next_results[fake->next_result_index++];
	}

	if (rc != 0) {
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
		return -ECANCELED;
	}

	if (fake->block_until_pause) {
		while (request->downloader->state != SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
			if (request->downloader->cancel_requested) {
				fake->cancel_observed = true;
				return -ECANCELED;
			}

			k_sleep(K_MSEC(10));
		}

		fake->pause_observed = true;

		while (request->downloader->state == SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
			if (request->downloader->cancel_requested) {
				fake->cancel_observed = true;
				return -ECANCELED;
			}

			k_sleep(K_MSEC(10));
		}
	}

	if (fake->payload != NULL && fake->payload_len > 0) {
		struct spotflow_artifact_block block = {
			.offset = 0,
			.data = fake->payload,
			.data_len = fake->payload_len,
			.is_last = true,
		};

		request->callback(&block, request->downloader, request->callback_ctx);

		if (request->bytes_downloaded != NULL) {
			*request->bytes_downloaded = fake->payload_len;
		}
	} else if (request->bytes_downloaded != NULL) {
		*request->bytes_downloaded = 0;
	}

	return 0;
}
