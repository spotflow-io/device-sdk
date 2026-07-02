#include <errno.h>
#include <string.h>

#include "spotflow_build_id.h"
#include "spotflow_ota_build_id_fake.h"

static struct spotflow_ota_build_id_fake build_id_fake;

struct spotflow_ota_build_id_fake* spotflow_ota_build_id_fake_get(void)
{
	return &build_id_fake;
}

void spotflow_ota_build_id_fake_reset(struct spotflow_ota_build_id_fake* fake)
{
	memset(fake, 0, sizeof(*fake));
	fake->get_result = -ENOSYS;
}

void spotflow_ota_build_id_fake_set_running_build_id(
    const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	struct spotflow_ota_build_id_fake* fake = spotflow_ota_build_id_fake_get();

	if (build_id == NULL) {
		fake->has_running_build_id = false;
		fake->get_result = -ENOSYS;
		return;
	}

	memcpy(fake->running_build_id, build_id, SPOTFLOW_BUILD_ID_LENGTH);
	fake->has_running_build_id = true;
	fake->get_result = 0;
}

int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len)
{
	struct spotflow_ota_build_id_fake* fake = spotflow_ota_build_id_fake_get();

	if (fake->get_result != 0) {
		*build_id = NULL;
		*build_id_len = 0;
		return fake->get_result;
	}

	if (!fake->has_running_build_id) {
		*build_id = NULL;
		*build_id_len = 0;
		return -ENOSYS;
	}

	*build_id = fake->running_build_id;
	*build_id_len = SPOTFLOW_BUILD_ID_LENGTH;
	return 0;
}
