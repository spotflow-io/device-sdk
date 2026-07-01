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
	size_t last_range_start;
	bool tls;
	uint16_t port;
	size_t partial_transient_fail_after_bytes;
	/** errno for partial fail; 0 keeps legacy -EAGAIN with transient_failure set. */
	int partial_fail_errno;
	/** When non-zero, deliver payload in chunks of this size (for cancel tests). */
	size_t chunk_size;
	/** Bytes delivered in the last transport_download call. */
	size_t bytes_delivered;
	bool block_until_cancel;
	bool cancel_observed;
	bool block_until_pause;
	bool pause_observed;
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
