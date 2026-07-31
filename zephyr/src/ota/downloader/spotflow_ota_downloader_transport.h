#ifndef SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H
#define SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spotflow/downloader.h>

#include "ota/downloader/spotflow_ota_url.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_downloader_transport_request {
	const struct spotflow_ota_url* url;
	const char* authorization_header;
	struct spotflow_downloader* downloader;
	spotflow_download_block_callback callback;
	void* callback_ctx;
	/** Absolute byte offset in the artifact to start reading from. */
	size_t range_start;
	/** Bytes delivered during this transport attempt. */
	size_t* bytes_downloaded;
	/**
	 * Artifact size learned from a previous ranged response, or zero when unknown.
	 * Ranged responses must report the same total size across retries.
	 */
	uint64_t* artifact_size;
	/** Set when this attempt was interrupted by a pause request. */
	bool* paused;
	bool* transient_failure;
};

int spotflow_ota_downloader_transport_download(
	struct spotflow_ota_downloader_transport_request* request);

/** Sets *transient_failure when err is retryable for the given progress in this attempt. */
void spotflow_ota_downloader_transport_note_error(
	struct spotflow_ota_downloader_transport_request* request, size_t bytes_in_attempt,
	int err);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H */
