#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/bindesc.h>
#include <zephyr/logging/log.h>

#include "spotflow_build_id.h"
#include "ota/spotflow_ota_identity.h"
#include "ota/spotflow_ota_platform.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define SPOTFLOW_BINDESC_ID_BUILD_ID 0x5f0
#define BINDESC_SCAN_CHUNK_SIZE 256

static bool build_id_is_all_zero(const uint8_t* build_id, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (build_id[i] != 0) {
			return false;
		}
	}

	return true;
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

static int read_build_id_from_bindesc_handle(struct bindesc_handle* handle,
					     uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	const uint8_t* bytes;
	size_t size;
	int rc;

	rc = bindesc_find_bytes(handle, SPOTFLOW_BINDESC_ID_BUILD_ID, &bytes, &size);
	if (rc != 0) {
		return rc;
	}

	if (size != SPOTFLOW_BUILD_ID_LENGTH) {
		return -EINVAL;
	}

	memcpy(build_id, bytes, SPOTFLOW_BUILD_ID_LENGTH);

	if (build_id_is_all_zero(build_id, SPOTFLOW_BUILD_ID_LENGTH)) {
		return -ENOSYS;
	}

	return 0;
}

static int find_bindesc_offset(size_t image_start, size_t image_size, size_t* bindesc_offset)
{
	uint8_t chunk[BINDESC_SCAN_CHUNK_SIZE];
	const size_t magic_size = sizeof(uint64_t);
	const size_t scan_end = image_start + image_size;

	for (size_t offset = image_start; offset + magic_size <= scan_end;) {
		size_t chunk_len = scan_end - offset;
		int rc;

		if (chunk_len > sizeof(chunk)) {
			chunk_len = sizeof(chunk);
		}

		rc = spotflow_ota_platform_read_upload_slot(offset, chunk, chunk_len);
		if (rc != 0) {
			return rc;
		}

		for (size_t i = 0; i + magic_size <= chunk_len; i += BINDESC_ALIGNMENT) {
			const size_t absolute_offset = offset + i;

			if ((absolute_offset % BINDESC_ALIGNMENT) != 0) {
				continue;
			}

			uint64_t magic;

			memcpy(&magic, chunk + i, magic_size);
			if (magic == BINDESC_MAGIC) {
				*bindesc_offset = absolute_offset;
				return 0;
			}
		}

		if (chunk_len < sizeof(chunk)) {
			break;
		}

		offset += chunk_len - (magic_size - 1);
		offset -= offset % BINDESC_ALIGNMENT;
	}

	return -ENOENT;
}

int spotflow_ota_identity_get_running_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (build_id == NULL) {
		return -EINVAL;
	}

	return copy_running_build_id(build_id);
}

int spotflow_ota_identity_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	struct bindesc_handle handle;
	size_t image_start;
	size_t image_size;
	size_t bindesc_offset = 0;
	int rc;

	if (build_id == NULL) {
		return -EINVAL;
	}

	rc = spotflow_ota_platform_get_upload_image_info(&image_start, &image_size);
	if (rc != 0) {
		return rc;
	}

	rc = find_bindesc_offset(image_start, image_size, &bindesc_offset);
	if (rc != 0) {
		return rc;
	}

	rc = spotflow_ota_platform_bindesc_open_upload(&handle, bindesc_offset);
	if (rc != 0) {
		return rc;
	}

	return read_build_id_from_bindesc_handle(&handle, build_id);
}

enum spotflow_ota_identity_cmp
spotflow_ota_identity_compare_probation(const uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	uint8_t running_build_id[SPOTFLOW_BUILD_ID_LENGTH];
	int rc;

	if (expected_build_id == NULL) {
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
