#ifndef SPOTFLOW_OTA_DOWNLOADER_H
#define SPOTFLOW_OTA_DOWNLOADER_H

#include <stddef.h>

#include <spotflow/downloader.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_DOWNLOAD_SECRET_MAX_LENGTH 24

int spotflow_ota_downloader_build_authorization_header(const char* secret, char* out,
						       size_t out_len);

typedef void (*spotflow_ota_download_started_callback)(struct spotflow_downloader* downloader,
						       void* callback_ctx);

int spotflow_ota_download_artifact(struct spotflow_downloader* downloader,
				   const struct spotflow_download_request* request,
				   spotflow_download_block_callback callback, void* callback_ctx,
				   spotflow_ota_download_started_callback started_callback,
				   void* started_callback_ctx);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_H */
