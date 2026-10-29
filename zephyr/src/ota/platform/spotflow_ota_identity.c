#include "ota/platform/spotflow_ota_identity.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

static int copy_running_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

int spotflow_ota_identity_get_running_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (build_id == NULL) {
		LOG_ERR("build_id cannot be NULL");
		return -EINVAL;
	}

	return copy_running_build_id(build_id);
}

enum spotflow_ota_identity_cmp
spotflow_ota_identity_compare_probation(const uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	uint8_t running_build_id[SPOTFLOW_BUILD_ID_LENGTH];
	int rc;

	if (expected_build_id == NULL) {
		LOG_ERR("expected_build_id cannot be NULL");
		return SPOTFLOW_OTA_IDENTITY_UNAVAILABLE;
	}

	rc = copy_running_build_id(running_build_id);
	if (rc != 0) {
		return SPOTFLOW_OTA_IDENTITY_UNAVAILABLE;
	}

	if (memcmp(expected_build_id, running_build_id, SPOTFLOW_BUILD_ID_LENGTH) == 0) {
		return SPOTFLOW_OTA_IDENTITY_MATCH;
	}

	return SPOTFLOW_OTA_IDENTITY_MISMATCH;
}

static int copy_running_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	const uint8_t* id_bytes;
	uint16_t id_len;
	int rc = spotflow_build_id_get(&id_bytes, &id_len);

	if (rc != 0) {
		return rc;
	}

	if (id_len != SPOTFLOW_BUILD_ID_LENGTH) {
		return -EINVAL;
	}

	memcpy(build_id, id_bytes, SPOTFLOW_BUILD_ID_LENGTH);

	return 0;
}
