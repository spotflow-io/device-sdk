#ifndef SPOTFLOW_OTA_BUILD_ID_FAKE_H
#define SPOTFLOW_OTA_BUILD_ID_FAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "spotflow_build_id.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_ota_build_id_fake {
	bool has_running_build_id;
	uint8_t running_build_id[SPOTFLOW_BUILD_ID_LENGTH];
	int get_result;
};

void spotflow_ota_build_id_fake_reset(struct spotflow_ota_build_id_fake* fake);

struct spotflow_ota_build_id_fake* spotflow_ota_build_id_fake_get(void);

void spotflow_ota_build_id_fake_set_running_build_id(
    const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_BUILD_ID_FAKE_H */
