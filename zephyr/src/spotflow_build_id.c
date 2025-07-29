#include <zephyr/bindesc.h>

#include "spotflow_build_id.h"

#define SPOTFLOW_BINDESC_ID_BUILD_ID 0x5f0

/* The actual build ID will be written to this array in a post-build command */
BINDESC_BYTES_DEFINE(spotflow_build_id, SPOTFLOW_BINDESC_ID_BUILD_ID,
		     ({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }));

void spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len)
{
	*build_id = BINDESC_GET_BYTES(spotflow_build_id);
	*build_id_len = BINDESC_GET_SIZE(spotflow_build_id);
}
