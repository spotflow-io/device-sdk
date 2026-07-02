#include <errno.h>
#include <stdbool.h>

#include <zephyr/logging/log.h>

#include "ota/downloader/spotflow_ota_downloader_transport.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static bool transport_error_is_transient(int err, size_t bytes_downloaded)
{
	switch (err) {
	case -EHOSTUNREACH:
	case -ECONNRESET:
	case -ETIMEDOUT:
	case -ECONNREFUSED:
	case -ENOTCONN:
	case -EAGAIN:
		return true;
	default:
		break;
	}

	/* Connection or parser errors after partial body data can be resumed with HTTP Range. */
	if (bytes_downloaded > 0) {
		switch (err) {
		case -EBADMSG:
		case -ECONNABORTED:
			return true;
		default:
			break;
		}
	}

	return false;
}

void spotflow_ota_downloader_transport_note_error(
    struct spotflow_ota_downloader_transport_request* request, size_t bytes_in_attempt, int err)
{
	if (request == NULL || request->transient_failure == NULL || *request->transient_failure) {
		return;
	}

	if (!transport_error_is_transient(err, bytes_in_attempt)) {
		return;
	}

	*request->transient_failure = true;

	if (bytes_in_attempt > 0) {
		LOG_DBG("Artifact download connection lost after %zu bytes in this request (%d), "
			"will resume from byte %zu",
			bytes_in_attempt, err, request->range_start + bytes_in_attempt);
	}
}
