#include "ota/platform/spotflow_ota_platform.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/bindesc.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

#define SPOTFLOW_BINDESC_ID_BUILD_ID 0x5f0
#define BINDESC_SCAN_CHUNK_SIZE 256

static bool build_id_is_all_zero(const uint8_t* build_id, size_t len);
static int read_build_id_from_bindesc_handle(struct bindesc_handle* handle,
					     uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);
static int find_bindesc_offset(size_t image_start, size_t image_size, size_t* bindesc_offset);

static struct flash_img_context upload_ctx;

int spotflow_ota_platform_request_upgrade(void)
{
	return boot_request_upgrade(BOOT_UPGRADE_TEST);
}

int spotflow_ota_platform_confirm_image(bool* reboot_required)
{
	if (reboot_required == NULL) {
		return -EINVAL;
	}
	*reboot_required = false;
	return boot_write_img_confirmed();
}

int spotflow_ota_platform_init(void)
{
	return 0;
}

int spotflow_ota_platform_get_running_image_state(enum spotflow_ota_running_image_state* state)
{
	if (state == NULL) {
		return -EINVAL;
	}
	*state = boot_is_img_confirmed() ? SPOTFLOW_OTA_RUNNING_IMAGE_CONFIRMED
					 : SPOTFLOW_OTA_RUNNING_IMAGE_UNCONFIRMED;
	return 0;
}

int spotflow_ota_platform_reboot(void)
{
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}

static int spotflow_ota_platform_read_upload_slot(size_t offset, uint8_t* dst, size_t len)
{
	const struct flash_area* fa;
	uint8_t slot_id = flash_img_get_upload_slot();
	int rc = flash_area_open(slot_id, &fa);

	if (rc != 0) {
		return rc;
	}

	if (offset + len > fa->fa_size) {
		flash_area_close(fa);
		return -EINVAL;
	}

	rc = flash_area_read(fa, offset, dst, len);
	flash_area_close(fa);

	return rc;
}

static int spotflow_ota_platform_get_upload_image_info(size_t* image_start, size_t* image_size)
{
	struct mcuboot_img_header header;
	uint8_t slot_id = flash_img_get_upload_slot();
	int rc = boot_read_bank_header(slot_id, &header, sizeof(header));

	if (rc != 0) {
		return rc;
	}

	if (image_start != NULL) {
		*image_start = boot_get_image_start_offset(slot_id);
	}

	if (image_size != NULL) {
		*image_size = header.h.v1.image_size;
	}

	return 0;
}

static int spotflow_ota_platform_bindesc_open_upload(struct bindesc_handle* handle,
						     size_t partition_offset)
{
#if !IS_ENABLED(CONFIG_BINDESC_READ_FLASH)
	ARG_UNUSED(handle);
	ARG_UNUSED(partition_offset);

	return -ENOTSUP;
#else
	const struct flash_area* fa;
	uint8_t slot_id = flash_img_get_upload_slot();
	int rc = flash_area_open(slot_id, &fa);

	if (rc != 0) {
		return rc;
	}

	rc = bindesc_open_flash(handle, fa->fa_off + partition_offset, fa->fa_dev);
	flash_area_close(fa);

	return rc;
#endif /* CONFIG_BINDESC_READ_FLASH */
}

int spotflow_ota_platform_begin_image_write(void)
{
	/* An unconfirmed image may still need the upload slot contents for rollback. */
	if (!boot_is_img_confirmed()) {
		LOG_ERR("Cannot overwrite the upload slot while the running image is unconfirmed");
		return -EBUSY;
	}

	return flash_img_init(&upload_ctx);
}

int spotflow_ota_platform_write_image_block(const struct spotflow_artifact_block* block)
{
	if (block == NULL || (block->data == NULL && block->data_len > 0)) {
		return -EINVAL;
	}

	return flash_img_buffered_write(&upload_ctx, block->data, block->data_len, block->is_last);
}

int spotflow_ota_platform_finish_image_write(void)
{
	return 0;
}

int spotflow_ota_platform_abort_image_write(void)
{
	/* flash_img_init() resets buffered writes before the next download. */
	return 0;
}

int spotflow_ota_platform_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	struct bindesc_handle handle;
	size_t image_start;
	size_t image_size;
	size_t bindesc_offset = 0;
	int rc;

	if (build_id == NULL) {
		LOG_ERR("build_id cannot be NULL");
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

static bool build_id_is_all_zero(const uint8_t* build_id, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (build_id[i] != 0) {
			return false;
		}
	}

	return true;
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
