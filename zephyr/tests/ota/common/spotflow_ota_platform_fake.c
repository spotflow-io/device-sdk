#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include <zephyr/bindesc.h>

#include "spotflow_ota_platform_fake.h"

static struct spotflow_ota_platform_fake platform_fake;

struct spotflow_ota_platform_fake* spotflow_ota_platform_fake_get(void)
{
	return &platform_fake;
}

void spotflow_ota_platform_fake_reset(struct spotflow_ota_platform_fake* fake)
{
	memset(fake, 0, sizeof(*fake));
}

int spotflow_ota_platform_request_test_upgrade(void)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	fake->upgrade_request_count++;

	return fake->upgrade_request_result;
}

int spotflow_ota_platform_confirm_image(void)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	fake->confirm_count++;
	if (fake->confirm_result == 0) {
		fake->image_confirmed = true;
	}

	return fake->confirm_result;
}

bool spotflow_ota_platform_is_image_confirmed(void)
{
	return spotflow_ota_platform_fake_get()->image_confirmed;
}

void spotflow_ota_platform_reboot(void)
{
	spotflow_ota_platform_fake_get()->reboot_count++;
}

int spotflow_ota_platform_read_upload_slot(size_t offset, uint8_t* dst, size_t len)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	if (fake->read_result != 0) {
		return fake->read_result;
	}

	if (offset + len > sizeof(fake->upload_slot)) {
		return -EINVAL;
	}

	memcpy(dst, fake->upload_slot + offset, len);

	return 0;
}

int spotflow_ota_platform_get_upload_image_info(size_t* image_start, size_t* image_size)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	if (fake->image_info_result != 0) {
		return fake->image_info_result;
	}

	if (image_start != NULL) {
		*image_start = fake->upload_image_start;
	}

	if (image_size != NULL) {
		*image_size = fake->upload_image_size;
	}

	return 0;
}

int spotflow_ota_platform_bindesc_open_upload(struct bindesc_handle* handle,
					      size_t partition_offset)
{
#if !IS_ENABLED(CONFIG_BINDESC_READ_RAM)
	ARG_UNUSED(handle);
	ARG_UNUSED(partition_offset);

	return -ENOTSUP;
#else
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();
	size_t image_end = fake->upload_image_start + fake->upload_image_size;
	size_t max_size;

	if (partition_offset >= image_end || partition_offset >= sizeof(fake->upload_slot)) {
		return -EINVAL;
	}

	max_size = image_end - partition_offset;
	if (max_size > sizeof(fake->upload_slot) - partition_offset) {
		max_size = sizeof(fake->upload_slot) - partition_offset;
	}

	return bindesc_open_ram(handle, fake->upload_slot + partition_offset, max_size);
#endif
}

int spotflow_ota_platform_begin_image_write(void)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	if (fake->begin_write_result != 0) {
		return fake->begin_write_result;
	}

	fake->upload_image_start = 0;
	fake->upload_image_size = 0;
	memset(fake->upload_slot, 0, sizeof(fake->upload_slot));

	return 0;
}

int spotflow_ota_platform_write_image_block(const uint8_t* data, size_t len, bool is_last)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	ARG_UNUSED(is_last);

	if (fake->write_fail_after_bytes > 0 &&
	    fake->upload_image_size >= fake->write_fail_after_bytes) {
		return fake->write_result != 0 ? fake->write_result : -ENOMEM;
	}

	if (fake->write_result != 0 && fake->write_fail_after_bytes == 0) {
		return fake->write_result;
	}

	if (data == NULL && len > 0) {
		return -EINVAL;
	}

	if (fake->upload_image_size + len > sizeof(fake->upload_slot)) {
		return -ENOMEM;
	}

	if (len > 0) {
		memcpy(fake->upload_slot + fake->upload_image_size, data, len);
		fake->upload_image_size += len;
	}

	return 0;
}
