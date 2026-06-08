#ifndef SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H
#define SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H

#include <stddef.h>

#include <spotflow/downloader.h>

#include "ota/spotflow_ota_url.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_downloader_transport_request {
	const struct ota_url* url;
	const char* authorization_header;
	struct spotflow_downloader* downloader;
	spotflow_download_block_callback callback;
	void* callback_ctx;
	size_t* bytes_downloaded;
	bool* transient_failure;
};

int spotflow_ota_downloader_transport_download(
    struct spotflow_ota_downloader_transport_request* request);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_H */
