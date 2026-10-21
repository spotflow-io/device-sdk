#ifndef SPOTFLOW_OTA_PSA_IDENTITY_H
#define SPOTFLOW_OTA_PSA_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "spotflow_build_id.h"

/* Stream only the application payload, excluding container headers/trailers.
 * Zero-initialize before use. Descriptor magic and entries may cross blocks.
 */
struct spotflow_ota_psa_identity {
	uint64_t magic;
	size_t offset;
	size_t remaining;
	uint8_t header[4];
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t used;
	uint8_t phase;
	int error;
};

void spotflow_ota_psa_identity_feed(struct spotflow_ota_psa_identity* ctx,
				   const uint8_t* data, size_t len);
int spotflow_ota_psa_identity_get(const struct spotflow_ota_psa_identity* ctx,
				uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);
bool spotflow_ota_psa_identity_matches(const uint8_t expected[SPOTFLOW_BUILD_ID_LENGTH]);

#endif /* SPOTFLOW_OTA_PSA_IDENTITY_H */
