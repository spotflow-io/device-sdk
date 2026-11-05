#include "spotflow_ota_platform_fake.h"

#include "ota/platform/spotflow_ota_platform.h"
#include "ota/platform/spotflow_ota_identity_stream.h"

#include <errno.h>
#include <string.h>

static struct spotflow_ota_platform_fake platform_fake;

struct spotflow_ota_platform_fake* spotflow_ota_platform_fake_get(void)
{
	return &platform_fake;
}

void spotflow_ota_platform_fake_reset(struct spotflow_ota_platform_fake* fake)
{
	memset(fake, 0, sizeof(*fake));
	k_sem_init(&fake->upgrade_request_entered, 0, 1);
	k_sem_init(&fake->continue_upgrade_request, 0, 1);
}

int spotflow_ota_platform_request_upgrade(void)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	fake->upgrade_request_count++;
	if (fake->block_upgrade_request) {
		k_sem_give(&fake->upgrade_request_entered);
		k_sem_take(&fake->continue_upgrade_request, K_FOREVER);
	}

	return fake->upgrade_request_result;
}

int spotflow_ota_platform_confirm_image(bool* reboot_required)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	if (reboot_required == NULL) {
		return -EINVAL;
	}
	*reboot_required = fake->confirmation_requires_reboot;
	fake->confirm_count++;
	if (fake->confirm_result == 0 && !fake->confirmation_requires_reboot) {
		fake->image_confirmed = true;
	}

	return fake->confirm_result;
}

int spotflow_ota_platform_init(void)
{
	return 0;
}

int spotflow_ota_platform_get_running_image_state(enum spotflow_ota_running_image_state* state)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	if (state == NULL) {
		return -EINVAL;
	}
	*state = fake->image_confirmed ? SPOTFLOW_OTA_RUNNING_IMAGE_CONFIRMED
				       : SPOTFLOW_OTA_RUNNING_IMAGE_UNCONFIRMED;
	return fake->query_result;
}

int spotflow_ota_platform_reboot(void)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	fake->reboot_count++;
	return fake->reboot_result;
}

int spotflow_ota_platform_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();
	struct spotflow_ota_identity_stream stream = { 0 };

	if (fake->image_info_result != 0) {
		return fake->image_info_result;
	}
	if (fake->read_result != 0) {
		return fake->read_result;
	}
	spotflow_ota_identity_stream_feed(&stream, fake->upload_slot + fake->upload_image_start,
					  fake->upload_image_size);
	return spotflow_ota_identity_stream_get(&stream, build_id);
}

int spotflow_ota_platform_finish_image_write(void)
{
	return spotflow_ota_platform_fake_get()->finish_result;
}

int spotflow_ota_platform_abort_image_write(void)
{
	spotflow_ota_platform_fake_get()->abort_count++;
	return 0;
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

int spotflow_ota_platform_write_image_block(const struct spotflow_artifact_block* block)
{
	struct spotflow_ota_platform_fake* fake = spotflow_ota_platform_fake_get();

	if (block == NULL) {
		return -EINVAL;
	}
	const uint8_t* data = block->data;
	size_t len = block->data_len;

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
