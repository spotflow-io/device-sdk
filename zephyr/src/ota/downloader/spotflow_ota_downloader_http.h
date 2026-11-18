#ifndef SPOTFLOW_OTA_DOWNLOADER_HTTP_H
#define SPOTFLOW_OTA_DOWNLOADER_HTTP_H

#include "ota/core/spotflow_ota_limits.h"
#include "ota/downloader/spotflow_ota_url.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPOTFLOW_OTA_HTTP_AUTHORIZATION_HEADER_MAX_LEN                                       \
	(sizeof("Authorization: OtaSecret ") - 1 + SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH + \
	 sizeof("\r\n"))

/** @brief HTTP-only request data; never log the authorization header. */
struct spotflow_ota_http_request {
	struct spotflow_ota_url url;
	char authorization_header[SPOTFLOW_OTA_HTTP_AUTHORIZATION_HEADER_MAX_LEN];
};

/**
 * @brief Prepare a socket request.
 * @param url Artifact URL.
 * @param secret Per-artifact download credential; never logged.
 * @param request Parsed URL and authorization header output.
 * @return 0 on success, -EINVAL for invalid input, or -ENOMEM for insufficient space.
 */
int spotflow_ota_downloader_prepare_http_request(const char* url, const char* secret,
						 struct spotflow_ota_http_request* request);
/**
 * @brief Build the HTTP credential header.
 * @param secret Per-artifact download credential; never logged.
 * @param out Output header buffer.
 * @param out_len Output buffer capacity, including the terminating NUL.
 * @return 0 on success, -EINVAL for invalid input, or -ENOMEM for insufficient space.
 */
int spotflow_ota_downloader_build_authorization_header(const char* secret, char* out,
						       size_t out_len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPOTFLOW_OTA_DOWNLOADER_HTTP_H */
