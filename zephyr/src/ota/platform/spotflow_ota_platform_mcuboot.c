#include "ota/platform/spotflow_ota_platform.h"

#include "ota/platform/mcuboot/spotflow_ota_mcuboot_stream.h"

#include <errno.h>
#include <string.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

static struct flash_img_context upload_context;
static struct spotflow_ota_mcuboot_stream image_stream;
static size_t upload_capacity;
static bool image_write_finished;

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

int spotflow_ota_platform_begin_image_write(void)
{
	/* Preserve the inactive image while it may still be needed for rollback. */
	if (!boot_is_img_confirmed()) {
		LOG_ERR("Cannot overwrite firmware storage while the running image is unconfirmed");
		return -EBUSY;
	}
	memset(&image_stream, 0, sizeof(image_stream));
	image_write_finished = false;
	int rc = flash_img_init(&upload_context);

	if (rc == 0) {
		upload_capacity = upload_context.flash_area->fa_size;
	}
	return rc;
}

int spotflow_ota_platform_write_image_block(const struct spotflow_artifact_block* block)
{
	int rc = spotflow_ota_mcuboot_stream_feed(&image_stream, block, upload_capacity);

	if (rc < 0) {
		return rc;
	}
	rc = flash_img_buffered_write(&upload_context, block->data, block->data_len,
				      block->is_last);
	image_write_finished = rc == 0 && block->is_last;
	return rc;
}

int spotflow_ota_platform_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	return spotflow_ota_mcuboot_stream_get_build_id(&image_stream, build_id);
}

int spotflow_ota_platform_finish_image_write(void)
{
	return image_write_finished ? 0 : -EBADMSG;
}

int spotflow_ota_platform_abort_image_write(void)
{
	if (upload_context.flash_area != NULL) {
		flash_area_close(upload_context.flash_area);
		upload_context.flash_area = NULL;
	}
	memset(&image_stream, 0, sizeof(image_stream));
	image_write_finished = false;
	return 0;
}
