#ifndef SPOTFLOW_OTA_DOWNLOADER_H
#define SPOTFLOW_OTA_DOWNLOADER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_DOWNLOAD_SECRET_MAX_LENGTH 24

int spotflow_ota_downloader_build_authorization_header(const char* secret, char* out,
						       size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_H */
