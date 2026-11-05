#ifndef SPOTFLOW_OTA_MCUBOOT_STREAM_H
#define SPOTFLOW_OTA_MCUBOOT_STREAM_H

#include "ota/platform/spotflow_ota_identity_stream.h"

#include <spotflow/downloader.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief MCUboot v1 stream framing; zero-initialize before each download. */
struct spotflow_ota_mcuboot_stream {
	struct spotflow_ota_identity_stream identity;
	uint8_t header[32];
	size_t offset;
	size_t payload_start;
	size_t payload_end;
	bool complete;
};

/**
 * @brief Feed sequential signed-image bytes, scanning only the executable payload.
 * @param stream Framing and descriptor context.
 * @param block Downloaded artifact block.
 * @param max_size Maximum complete artifact size in bytes.
 * @return Zero on success, negative errno for invalid framing or unsupported encoding.
 */
int spotflow_ota_mcuboot_stream_feed(struct spotflow_ota_mcuboot_stream* stream,
				     const struct spotflow_artifact_block* block, size_t max_size);

/**
 * @brief Copy a completed image's identity.
 * @param stream Framing and descriptor context.
 * @param build_id Receives the build ID.
 * @return Zero on success, negative errno for an incomplete or invalid identity.
 */
int spotflow_ota_mcuboot_stream_get_build_id(const struct spotflow_ota_mcuboot_stream* stream,
					     uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_MCUBOOT_STREAM_H */
