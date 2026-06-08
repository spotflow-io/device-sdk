#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "spotflow_build_id.h"
#include "ota/spotflow_ota_identity.h"
#include "ota/spotflow_ota_platform.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define SPOTFLOW_BINDESC_ID_BUILD_ID 0x5f0
#define SPOTFLOW_BINDESC_BUILD_ID_TAG ((uint16_t)((BINDESC_TYPE_BYTES << 12) | SPOTFLOW_BINDESC_ID_BUILD_ID))

#define BINDESC_TYPE_BYTES 0x2
#define BINDESC_ENTRY_HEADER_SIZE 4

static const uint8_t bindesc_magic[] = { 0x46, 0x60, 0xa4, 0x7e, 0x5a, 0x3e, 0x86, 0xb9 };

#define BINDESC_SCAN_BUFFER_SIZE 256
#define BINDESC_ALIGNMENT 4

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

static int read_build_id_from_bindesc(const uint8_t* bindesc_data, size_t bindesc_len,
				      uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	size_t offset;

	if (bindesc_len < sizeof(bindesc_magic)) {
		return -EINVAL;
	}

	if (memcmp(bindesc_data, bindesc_magic, sizeof(bindesc_magic)) != 0) {
		return -EINVAL;
	}

	offset = sizeof(bindesc_magic);

	while (offset + BINDESC_ENTRY_HEADER_SIZE <= bindesc_len) {
		uint16_t tag = bindesc_data[offset] | (bindesc_data[offset + 1] << 8);
		uint16_t len = bindesc_data[offset + 2] | (bindesc_data[offset + 3] << 8);

		if (tag == 0xffff) {
			break;
		}

		if (offset + BINDESC_ENTRY_HEADER_SIZE + len > bindesc_len) {
			return -EINVAL;
		}

		if (tag == SPOTFLOW_BINDESC_BUILD_ID_TAG) {
			if (len != SPOTFLOW_BUILD_ID_LENGTH) {
				return -EINVAL;
			}

			memcpy(build_id, bindesc_data + offset + BINDESC_ENTRY_HEADER_SIZE,
			       SPOTFLOW_BUILD_ID_LENGTH);

			if (build_id_is_all_zero(build_id, SPOTFLOW_BUILD_ID_LENGTH)) {
				return -ENOSYS;
			}

			return 0;
		}

		offset += WB_UP(BINDESC_ENTRY_HEADER_SIZE + len);
	}

	return -ENOENT;
}

static int find_bindesc_offset(size_t image_start, size_t image_size, size_t* bindesc_offset)
{
	uint8_t magic_buf[sizeof(bindesc_magic)];

	for (size_t offset = image_start; offset + sizeof(bindesc_magic) <= image_start + image_size;
	     offset += BINDESC_ALIGNMENT) {
		int rc = spotflow_ota_platform_read_upload_slot(offset, magic_buf, sizeof(magic_buf));

		if (rc != 0) {
			return rc;
		}

		if (memcmp(magic_buf, bindesc_magic, sizeof(bindesc_magic)) == 0) {
			*bindesc_offset = offset;
			return 0;
		}
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
	size_t image_start;
	size_t image_size;
	size_t bindesc_offset;
	size_t bindesc_len;
	uint8_t bindesc_buf[BINDESC_SCAN_BUFFER_SIZE];
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

	if (bindesc_offset < image_start ||
	    bindesc_offset - image_start >= image_size) {
		return -EINVAL;
	}

	bindesc_len = image_start + image_size - bindesc_offset;
	if (bindesc_len > sizeof(bindesc_buf)) {
		bindesc_len = sizeof(bindesc_buf);
	}

	rc = spotflow_ota_platform_read_upload_slot(bindesc_offset, bindesc_buf, bindesc_len);
	if (rc != 0) {
		return rc;
	}

	return read_build_id_from_bindesc(bindesc_buf, bindesc_len, build_id);
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
