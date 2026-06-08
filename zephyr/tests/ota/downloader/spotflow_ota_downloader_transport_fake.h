#ifndef SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_FAKE_H
#define SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_FAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_downloader_transport_fake {
	uint32_t call_count;
	int next_results[8];
	uint8_t next_result_count;
	uint8_t next_result_index;
	const uint8_t* payload;
	size_t payload_len;
	char last_authorization_header[96];
	char last_host[128];
	char last_path[128];
	bool tls;
	uint16_t port;
	bool block_until_cancel;
	bool cancel_observed;
};

void spotflow_ota_downloader_transport_fake_reset(
    struct spotflow_ota_downloader_transport_fake* fake);

void spotflow_ota_downloader_transport_fake_set_results(
    struct spotflow_ota_downloader_transport_fake* fake, const int* results, size_t count);

struct spotflow_ota_downloader_transport_fake* spotflow_ota_downloader_transport_fake_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_DOWNLOADER_TRANSPORT_FAKE_H */
