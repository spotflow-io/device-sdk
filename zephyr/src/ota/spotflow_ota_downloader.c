#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <spotflow/downloader.h>

#include "ota/spotflow_ota_downloader.h"
#include "ota/spotflow_ota_downloader_transport.h"
#include "ota/spotflow_ota_url.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

/*
 * Test-only aggregate of downloader log format strings. Keeping these in one place
 * makes it easy to verify that full URLs and secrets are not logged.
 */
#ifdef CONFIG_ZTEST
const char spotflow_ota_downloader_log_tags[] =
    "Starting artifact download (TLS: %d, port: %u)"
    "Artifact download finished (%zu bytes)"
    "Artifact download failed: %d"
    "Transient artifact download failure (%d), retrying";
#endif

#define OTA_AUTHORIZATION_HEADER_PREFIX "Authorization: OtaSecret "
#define OTA_AUTHORIZATION_HEADER_SUFFIX "\r\n"
#define OTA_AUTHORIZATION_HEADER_MAX_LEN                                                         \
	(sizeof(OTA_AUTHORIZATION_HEADER_PREFIX) - 1 + SPOTFLOW_OTA_DOWNLOAD_SECRET_MAX_LENGTH + \
	 sizeof(OTA_AUTHORIZATION_HEADER_SUFFIX))

static bool downloader_is_canceled(struct spotflow_downloader* downloader);
static void downloader_prepare(struct spotflow_downloader* downloader);
static void downloader_finish(struct spotflow_downloader* downloader);
static bool downloader_error_is_transient(int err, bool transient_failure);

int spotflow_ota_downloader_build_authorization_header(const char* secret, char* out,
						       size_t out_len)
{
	if (secret == NULL || out == NULL || out_len == 0) {
		return -EINVAL;
	}

	int written =
	    snprintk(out, out_len,
		     OTA_AUTHORIZATION_HEADER_PREFIX "%s" OTA_AUTHORIZATION_HEADER_SUFFIX, secret);

	if (written < 0 || (size_t)written >= out_len) {
		return -ENOMEM;
	}

	return 0;
}

enum spotflow_downloader_state
spotflow_get_downloader_state(const struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		return SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	}

	return downloader->state;
}

int spotflow_pause_download(struct spotflow_downloader* downloader)
{
	ARG_UNUSED(downloader);

	return -ENOTSUP;
}

int spotflow_resume_download(struct spotflow_downloader* downloader)
{
	ARG_UNUSED(downloader);

	return -ENOTSUP;
}

int spotflow_cancel_download(struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		return -EINVAL;
	}

	downloader_prepare(downloader);

	if (downloader->state == SPOTFLOW_DOWNLOADER_STATE_INACTIVE) {
		k_mutex_unlock(&downloader->mutex);
		return -EINVAL;
	}

	downloader->cancel_requested = true;
	downloader->state = SPOTFLOW_DOWNLOADER_STATE_CANCELING;
	k_mutex_unlock(&downloader->mutex);

	return 0;
}

int spotflow_download_artifact(struct spotflow_downloader* downloader,
			       const struct spotflow_download_request* request,
			       spotflow_download_block_callback callback, void* callback_ctx)
{
	if (downloader == NULL || request == NULL || request->url == NULL ||
	    request->secret == NULL || callback == NULL) {
		return -EINVAL;
	}

	struct ota_url url;
	int rc = spotflow_ota_parse_url(request->url, &url);

	if (rc < 0) {
		return rc;
	}

	char authorization_header[OTA_AUTHORIZATION_HEADER_MAX_LEN];

	rc = spotflow_ota_downloader_build_authorization_header(
	    request->secret, authorization_header, sizeof(authorization_header));
	if (rc < 0) {
		return rc;
	}

	downloader_prepare(downloader);

	if (downloader->state != SPOTFLOW_DOWNLOADER_STATE_INACTIVE) {
		k_mutex_unlock(&downloader->mutex);
		return -EBUSY;
	}

	downloader->cancel_requested = false;
	downloader->state = SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING;
	k_mutex_unlock(&downloader->mutex);

	LOG_INF("Starting artifact download (TLS: %d, port: %u)", url.tls, url.port);

	while (true) {
		if (downloader_is_canceled(downloader)) {
			downloader_finish(downloader);
			return -ECANCELED;
		}

		size_t bytes_downloaded = 0;
		bool transient_failure = false;
		struct spotflow_ota_downloader_transport_request transport_request = {
			.url = &url,
			.authorization_header = authorization_header,
			.downloader = downloader,
			.callback = callback,
			.callback_ctx = callback_ctx,
			.bytes_downloaded = &bytes_downloaded,
			.transient_failure = &transient_failure,
		};

		rc = spotflow_ota_downloader_transport_download(&transport_request);
		if (rc == 0) {
			LOG_INF("Artifact download finished (%zu bytes)", bytes_downloaded);
			downloader_finish(downloader);
			return 0;
		}

		if (rc == -ECANCELED || !downloader_error_is_transient(rc, transient_failure)) {
			LOG_ERR("Artifact download failed: %d", rc);
			downloader_finish(downloader);
			return rc;
		}

		LOG_WRN("Transient artifact download failure (%d), retrying", rc);
		k_sleep(K_MSEC(CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_DELAY_MS));
	}
}

static void downloader_prepare(struct spotflow_downloader* downloader)
{
	if (!downloader->mutex_initialized) {
		k_mutex_init(&downloader->mutex);
		downloader->mutex_initialized = true;
	}

	k_mutex_lock(&downloader->mutex, K_FOREVER);
}

static void downloader_finish(struct spotflow_downloader* downloader)
{
	k_mutex_lock(&downloader->mutex, K_FOREVER);
	downloader->state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	downloader->cancel_requested = false;
	k_mutex_unlock(&downloader->mutex);
}

static bool downloader_is_canceled(struct spotflow_downloader* downloader)
{
	bool canceled;

	k_mutex_lock(&downloader->mutex, K_FOREVER);
	canceled = downloader->cancel_requested;
	k_mutex_unlock(&downloader->mutex);

	return canceled;
}

static bool downloader_error_is_transient(int err, bool transient_failure)
{
	if (transient_failure) {
		return true;
	}

	switch (err) {
	case -EHOSTUNREACH:
	case -ECONNRESET:
	case -ETIMEDOUT:
	case -ECONNREFUSED:
	case -ENOTCONN:
	case -EAGAIN:
		return true;
	default:
		return false;
	}
}
