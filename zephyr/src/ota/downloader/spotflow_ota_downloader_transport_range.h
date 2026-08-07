#ifndef SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_RANGE_H
#define SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_RANGE_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/net/http/client.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_ota_downloader_transport_validate_range_response(const struct http_response* response,
							      size_t range_start,
							      uint64_t* artifact_size);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_RANGE_H */
