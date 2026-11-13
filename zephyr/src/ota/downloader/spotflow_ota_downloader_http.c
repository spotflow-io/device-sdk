#include "ota/downloader/spotflow_ota_downloader_http.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

#define OTA_AUTHORIZATION_HEADER_PREFIX "Authorization: OtaSecret "
#define OTA_AUTHORIZATION_HEADER_SUFFIX "\r\n"

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

int spotflow_ota_downloader_prepare_http_request(const char* url, const char* secret,
						 struct spotflow_ota_http_request* request)
{
	if (url == NULL || secret == NULL || request == NULL) {
		return -EINVAL;
	}

	int rc = spotflow_ota_parse_url(url, &request->url);

	if (rc != 0) {
		return rc;
	}
	return spotflow_ota_downloader_build_authorization_header(
		secret, request->authorization_header, sizeof(request->authorization_header));
}
