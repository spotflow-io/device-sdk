#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include "logging/spotflow_log_backend.h"
#include "buildid/spotflow_build_id.h"

#define SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER 0xF0, 0x25, 0x14, 0x00
#define SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER_SIZE 4
#define SPOTFLOW_BINDESC_BUILD_ID_VALUE_SIZE 20
#define SPOTFLOW_BINDESC_BUILD_ID_TOTAL \
	(SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER_SIZE + SPOTFLOW_BINDESC_BUILD_ID_VALUE_SIZE)

/* Important: name and layout must match what the Python script expects */
// __attribute__((used, aligned(4)))
const uint8_t bindesc_entry_spotflow_build_id[SPOTFLOW_BINDESC_BUILD_ID_TOTAL] = {
	SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER,
	/* 20 reserved bytes for SHA1 (patched by script) */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* pointer to build-id bytes (skip 4-byte header) */
static const uint8_t* spotflow_build_id_ptr =
    &bindesc_entry_spotflow_build_id[SPOTFLOW_BINDESC_BUILD_ID_MOCK_HEADER_SIZE];
static const uint16_t spotflow_build_id_len = SPOTFLOW_BINDESC_BUILD_ID_VALUE_SIZE;

/**
 * @brief To get the Build ID values
 * 
 * @param build_id build ID value
 * @param build_id_len Build ID length
 * @return int Errors
 */
int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len)
{
	bool is_all_zero = true;
	for (int i = 0; i < spotflow_build_id_len; i++) {
		if (spotflow_build_id_ptr[i] != 0) {
			is_all_zero = false;
			break;
		}
	}

	if (is_all_zero) {
		*build_id = NULL;
		*build_id_len = 0;
		return -ENOSYS;
	}

	*build_id = spotflow_build_id_ptr;
	*build_id_len = spotflow_build_id_len;
	return 0;
}
/**
 * @brief Print Build ID
 * 
 */
void spotflow_build_id_print(void)
{
	const uint8_t* id;
	uint16_t len;
	if (spotflow_build_id_get(&id, &len) == 0) {
		SPOTFLOW_LOG("Spotflow Build ID: ");
		for (int i = 0; i < len; i++)
			SPOTFLOW_LOG("%02x", id[i]);
		SPOTFLOW_LOG("\n");
	} else {
		SPOTFLOW_LOG("Spotflow Build ID not patched.\n");
	}
}
