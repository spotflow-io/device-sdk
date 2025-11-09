#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include "logging/spotflow_log_backend.h"
#include "buildid/spotflow_build_id.h"
#include "esp_app_desc.h"

#define BUILD_ID_LEN 32  // SHA256 digest length in bytes

/**
 * @brief To get the Build ID values
 * 
 * @param build_id build ID value
 * @param build_id_len Build ID length
 * @return int Errors
 */
int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len)
{
	 const esp_app_desc_t* app_desc = esp_app_get_description();
    if (!app_desc) {
        SPOTFLOW_LOG("Failed to get app description");
        return -1;
    }

    *build_id = app_desc->app_elf_sha256;
    *build_id_len = BUILD_ID_LEN;

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
