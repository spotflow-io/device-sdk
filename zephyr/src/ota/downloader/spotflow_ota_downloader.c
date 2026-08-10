#include "ota/downloader/spotflow_ota_downloader.h"

#include <spotflow/downloader.h>

#include "ota/downloader/spotflow_ota_downloader_transport.h"
#include "ota/downloader/spotflow_ota_url.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/iterable_sections.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

#define OTA_AUTHORIZATION_HEADER_PREFIX "Authorization: OtaSecret "
#define OTA_AUTHORIZATION_HEADER_SUFFIX "\r\n"
#define OTA_AUTHORIZATION_HEADER_MAX_LEN                                                         \
	(sizeof(OTA_AUTHORIZATION_HEADER_PREFIX) - 1 + SPOTFLOW_OTA_DOWNLOAD_SECRET_MAX_LENGTH + \
	 sizeof(OTA_AUTHORIZATION_HEADER_SUFFIX))

static void downloader_wake_waiters(struct spotflow_downloader* downloader);
static void downloader_drain_resume_sem(struct spotflow_downloader* downloader);
static int downloader_wait_if_paused(struct spotflow_downloader* downloader);
static int downloader_wait_for_retry(struct spotflow_downloader* downloader, uint32_t delay_ms);
static int downloader_finish(struct spotflow_downloader* downloader, int result);
static int init_static_downloaders(void);

SYS_INIT(init_static_downloaders, PRE_KERNEL_2, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

uint32_t spotflow_ota_downloader_retry_delay_ms(uint32_t retry_ceiling_ms, uint32_t random_value)
{
	uint32_t minimum_delay_ms = (retry_ceiling_ms + 1U) / 2U;
	uint32_t jitter_range_ms = retry_ceiling_ms - minimum_delay_ms + 1U;

	return minimum_delay_ms + random_value % jitter_range_ms;
}

uint32_t spotflow_ota_downloader_next_retry_ceiling_ms(uint32_t retry_ceiling_ms)
{
	const uint32_t max_delay_ms = CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_MAX_DELAY_MS;

	if (retry_ceiling_ms >= max_delay_ms || retry_ceiling_ms > max_delay_ms / 2U) {
		return max_delay_ms;
	}

	return retry_ceiling_ms * 2U;
}

int spotflow_ota_downloader_build_authorization_header(const char* secret, char* out,
						       size_t out_len)
{
	/* secret is used only for the HTTP Authorization header and must not be logged. */
	if (secret == NULL || out == NULL || out_len == 0) {
		LOG_ERR("secret and out cannot be NULL, out_len cannot be 0");
		return -EINVAL;
	}

	int written = snprintk(out, out_len,
			       OTA_AUTHORIZATION_HEADER_PREFIX "%s" OTA_AUTHORIZATION_HEADER_SUFFIX,
			       secret);

	if (written < 0 || (size_t)written >= out_len) {
		return -ENOMEM;
	}

	return 0;
}

int spotflow_init_downloader(struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		LOG_ERR("downloader cannot be NULL");
		return -EINVAL;
	}

	int rc = k_mutex_init(&downloader->mutex);

	if (rc < 0) {
		return rc;
	}

	rc = k_sem_init(&downloader->resume_sem, 0, 1);
	if (rc < 0) {
		return rc;
	}

	downloader->state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	downloader->cancel_requested = false;

	return 0;
}

enum spotflow_downloader_state spotflow_get_downloader_state(struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		LOG_ERR("downloader cannot be NULL");
		return SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	}

	k_mutex_lock(&downloader->mutex, K_FOREVER);
	enum spotflow_downloader_state state = downloader->state;
	k_mutex_unlock(&downloader->mutex);

	return state;
}

int spotflow_pause_download(struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		LOG_ERR("downloader cannot be NULL");
		return -EINVAL;
	}

	k_mutex_lock(&downloader->mutex, K_FOREVER);

	if (downloader->state != SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING) {
		k_mutex_unlock(&downloader->mutex);
		return -EINVAL;
	}

	downloader->state = SPOTFLOW_DOWNLOADER_STATE_PAUSED;
	k_mutex_unlock(&downloader->mutex);

	return 0;
}

int spotflow_resume_download(struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		LOG_ERR("downloader cannot be NULL");
		return -EINVAL;
	}

	k_mutex_lock(&downloader->mutex, K_FOREVER);

	if (downloader->state != SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
		k_mutex_unlock(&downloader->mutex);
		return -EINVAL;
	}

	downloader->state = SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING;
	k_mutex_unlock(&downloader->mutex);

	downloader_wake_waiters(downloader);

	return 0;
}

int spotflow_cancel_download(struct spotflow_downloader* downloader)
{
	if (downloader == NULL) {
		LOG_ERR("downloader cannot be NULL");
		return -EINVAL;
	}

	k_mutex_lock(&downloader->mutex, K_FOREVER);

	if (downloader->state == SPOTFLOW_DOWNLOADER_STATE_INACTIVE) {
		k_mutex_unlock(&downloader->mutex);
		return -EINVAL;
	}

	downloader->cancel_requested = true;
	downloader->state = SPOTFLOW_DOWNLOADER_STATE_CANCELING;
	k_mutex_unlock(&downloader->mutex);

	downloader_wake_waiters(downloader);

	return 0;
}

int spotflow_download_artifact(struct spotflow_downloader* downloader,
			       const struct spotflow_download_request* request,
			       spotflow_download_block_callback callback, void* callback_ctx)
{
	return spotflow_ota_download_artifact(downloader, request, callback, callback_ctx, NULL,
					      NULL);
}

int spotflow_ota_download_artifact(struct spotflow_downloader* downloader,
				   const struct spotflow_download_request* request,
				   spotflow_download_block_callback callback, void* callback_ctx,
				   spotflow_ota_download_started_callback started_callback,
				   void* started_callback_ctx)
{
	if (downloader == NULL || request == NULL || request->url == NULL ||
	    request->secret == NULL || callback == NULL) {
		LOG_ERR("downloader, request, request->url, request->secret and callback cannot "
			"be NULL");
		return -EINVAL;
	}

	struct spotflow_ota_url url;
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

	k_mutex_lock(&downloader->mutex, K_FOREVER);

	if (downloader->state != SPOTFLOW_DOWNLOADER_STATE_INACTIVE) {
		k_mutex_unlock(&downloader->mutex);
		return -EBUSY;
	}

	downloader->cancel_requested = false;
	downloader_drain_resume_sem(downloader);
	downloader->state = SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING;
	k_mutex_unlock(&downloader->mutex);

	if (started_callback != NULL) {
		started_callback(downloader, started_callback_ctx);
	}

	LOG_DBG("Starting artifact download (TLS: %d, port: %u)", url.tls, url.port);

	size_t total_bytes_downloaded = 0;
	uint64_t artifact_size = 0;
	uint32_t retry_count = 0;
	uint32_t retry_ceiling_ms = CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_INITIAL_DELAY_MS;

	while (true) {
		if (downloader_wait_if_paused(downloader) != 0) {
			return downloader_finish(downloader, -ECANCELED);
		}

		size_t attempt_bytes = 0;
		bool paused = false;
		bool transient_failure = false;
		struct spotflow_ota_downloader_transport_request transport_request = {
			.url = &url,
			.authorization_header = authorization_header,
			.downloader = downloader,
			.callback = callback,
			.callback_ctx = callback_ctx,
			.range_start = total_bytes_downloaded,
			.bytes_downloaded = &attempt_bytes,
			.artifact_size = &artifact_size,
			.paused = &paused,
			.transient_failure = &transient_failure,
		};

		if (total_bytes_downloaded > 0) {
			LOG_DBG("Resuming artifact download from byte %zu", total_bytes_downloaded);
		}

		rc = spotflow_ota_downloader_transport_download(&transport_request);
		total_bytes_downloaded += attempt_bytes;

		if (paused && downloader_wait_if_paused(downloader) != 0) {
			return downloader_finish(downloader, -ECANCELED);
		}

		if (paused) {
			continue;
		}

		if (rc == 0) {
			rc = downloader_finish(downloader, 0);
			if (rc == 0) {
				LOG_DBG("Artifact download finished (%zu bytes)",
					total_bytes_downloaded);
			}
			return rc;
		}

		if (rc == -ECANCELED || !transient_failure) {
			LOG_ERR("Artifact download failed: %d", rc);
			return downloader_finish(downloader, rc);
		}

		uint32_t retry_delay_ms =
			spotflow_ota_downloader_retry_delay_ms(retry_ceiling_ms, sys_rand32_get());

		if (retry_count < UINT32_MAX) {
			retry_count++;
		}

		LOG_WRN("Transient artifact download failure (%d) after %zu bytes; retry %u in "
			"%u ms using Range",
			rc, total_bytes_downloaded, retry_count, retry_delay_ms);

		rc = downloader_wait_for_retry(downloader, retry_delay_ms);
		if (rc != 0) {
			return downloader_finish(downloader, rc);
		}

		retry_ceiling_ms = spotflow_ota_downloader_next_retry_ceiling_ms(retry_ceiling_ms);
	}
}

int spotflow_ota_downloader_check_state(struct spotflow_downloader* downloader)
{
	k_mutex_lock(&downloader->mutex, K_FOREVER);
	bool paused = downloader->state == SPOTFLOW_DOWNLOADER_STATE_PAUSED;
	bool canceled = downloader->cancel_requested;
	k_mutex_unlock(&downloader->mutex);

	if (canceled) {
		return -ECANCELED;
	}

	if (paused) {
		return -EAGAIN;
	}

	return 0;
}

static int downloader_finish(struct spotflow_downloader* downloader, int result)
{
	while (true) {
		k_mutex_lock(&downloader->mutex, K_FOREVER);

		if (downloader->cancel_requested) {
			result = -ECANCELED;
		}

		if (downloader->state != SPOTFLOW_DOWNLOADER_STATE_PAUSED) {
			downloader->state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
			downloader->cancel_requested = false;
			k_mutex_unlock(&downloader->mutex);
			return result;
		}

		k_mutex_unlock(&downloader->mutex);
		k_sem_take(&downloader->resume_sem, K_FOREVER);
	}
}

static void downloader_wake_waiters(struct spotflow_downloader* downloader)
{
	k_sem_give(&downloader->resume_sem);
}

static void downloader_drain_resume_sem(struct spotflow_downloader* downloader)
{
	k_sem_take(&downloader->resume_sem, K_NO_WAIT);
}

static int downloader_wait_if_paused(struct spotflow_downloader* downloader)
{
	while (true) {
		int rc = spotflow_ota_downloader_check_state(downloader);

		if (rc != -EAGAIN) {
			return rc;
		}

		k_sem_take(&downloader->resume_sem, K_FOREVER);
	}
}

static int downloader_wait_for_retry(struct spotflow_downloader* downloader, uint32_t delay_ms)
{
	k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(delay_ms));

	while (true) {
		/* The semaphore only signals a possible state change. Recheck the state and the
		 * original deadline after every wake so pause/resume cannot shorten the backoff.
		 */
		int rc = downloader_wait_if_paused(downloader);

		if (rc != 0) {
			return rc;
		}

		k_timeout_t remaining = sys_timepoint_timeout(deadline);

		if (K_TIMEOUT_EQ(remaining, K_NO_WAIT)) {
			return 0;
		}

		(void)k_sem_take(&downloader->resume_sem, remaining);
	}
}

static int init_static_downloaders(void)
{
	STRUCT_SECTION_FOREACH(spotflow_downloader, downloader)
	{
		int rc = spotflow_init_downloader(downloader);

		if (rc < 0) {
			return rc;
		}
	}

	return 0;
}
