#include <zephyr/bindesc.h>
#include <stdbool.h>
#include <errno.h>

#include "spotflow_build_id.h"

#define SPOTFLOW_BINDESC_ID_BUILD_ID 0x5f0

/* The actual build ID will be written to this array in a post-build command */
#define SPOTFLOW_BINDESC_ID_BUILD_ID_ORIGINAL_CONTENT \
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

#ifdef CONFIG_BINDESC

BINDESC_BYTES_DEFINE(spotflow_build_id, SPOTFLOW_BINDESC_ID_BUILD_ID,
		     ({ SPOTFLOW_BINDESC_ID_BUILD_ID_ORIGINAL_CONTENT }));

#else

#define SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER 0xf0, 0x25, 0x14, 0x00

#define SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER_SIZE 4

/* If binary descriptors are not supported, we create a symbol with the same content (4 bytes
 * header + 20 empty bytes reserved for the build ID) as if they were supported. This enables
 * us to use the same patching logic for both cases.
 */
const uint8_t bindesc_entry_spotflow_build_id[] = { SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER,
						    SPOTFLOW_BINDESC_ID_BUILD_ID_ORIGINAL_CONTENT };

#endif

int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len)
{
#ifdef CONFIG_BINDESC

	const uint8_t* id_bytes = BINDESC_GET_BYTES(spotflow_build_id);
	const uint16_t id_len = BINDESC_GET_SIZE(spotflow_build_id);

#else

	const uint8_t* id_bytes =
	    bindesc_entry_spotflow_build_id + SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER_SIZE;
	const uint16_t id_len =
	    sizeof(bindesc_entry_spotflow_build_id) - SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER_SIZE;

#endif

	bool is_all_zero = true;
	for (uint16_t i = 0; i < id_len; i++) {
		if (id_bytes[i] != 0) {
			is_all_zero = false;
			break;
		}
	}

	/* Return error if the build ID was not patched (all zeroes) */
	if (is_all_zero) {
		*build_id = NULL;
		*build_id_len = 0;
		return -ENOSYS;
	}

	*build_id = id_bytes;
	*build_id_len = id_len;
	return 0;
}
