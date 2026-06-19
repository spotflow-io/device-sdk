#include <stdbool.h>
#include <errno.h>
#include <stddef.h>

#include <zephyr/bindesc.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include "ota/platform/spotflow_ota_platform.h"

static struct flash_img_context upload_ctx;

int spotflow_ota_platform_request_test_upgrade(void)
{
	return boot_request_upgrade(BOOT_UPGRADE_TEST);
}

int spotflow_ota_platform_confirm_image(void)
{
	return boot_write_img_confirmed();
}

bool spotflow_ota_platform_is_image_confirmed(void)
{
	return boot_is_img_confirmed();
}

void spotflow_ota_platform_reboot(void)
{
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}

int spotflow_ota_platform_read_upload_slot(size_t offset, uint8_t* dst, size_t len)
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

int spotflow_ota_platform_get_upload_image_info(size_t* image_start, size_t* image_size)
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

int spotflow_ota_platform_bindesc_open_upload(struct bindesc_handle* handle,
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
#endif
}

int spotflow_ota_platform_begin_image_write(void)
{
	return flash_img_init(&upload_ctx);
}

int spotflow_ota_platform_write_image_block(const uint8_t* data, size_t len, bool is_last)
{
	if (data == NULL && len > 0) {
		return -EINVAL;
	}

	return flash_img_buffered_write(&upload_ctx, data, len, is_last);
}
