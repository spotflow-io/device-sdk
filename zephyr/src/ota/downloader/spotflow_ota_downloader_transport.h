#ifndef SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H
#define SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H

#include <stddef.h>

#include <spotflow/downloader.h>

#include "ota/downloader/spotflow_ota_url.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_downloader_transport_request {
	const struct ota_url* url;
	const char* authorization_header;
	struct spotflow_downloader* downloader;
	spotflow_download_block_callback callback;
	void* callback_ctx;
	/** Absolute byte offset in the artifact to start reading from. */
	size_t range_start;
	/** Bytes delivered during this transport attempt. */
	size_t* bytes_downloaded;
	bool* transient_failure;
};

int spotflow_ota_downloader_transport_download(
    struct spotflow_ota_downloader_transport_request* request);

/** Sets *transient_failure when err is retryable for the given progress in this attempt. */
void spotflow_ota_downloader_transport_note_error(
    struct spotflow_ota_downloader_transport_request* request, size_t bytes_in_attempt, int err);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H */
