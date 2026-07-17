#ifndef SPOTFLOW_BUILD_ID_H
#define SPOTFLOW_BUILD_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_BUILD_ID_LENGTH 20

/**
 * @brief Get the build ID embedded in the firmware image.
 *
 * @param build_id Output pointer set to the address of the build ID bytes
 * @param build_id_len Output pointer set to the length of the build ID in bytes
 *
 * @return 0 on success, negative errno on failure
 *         -ENOSYS: Build ID was not patched into the image (still all zeroes)
 */
int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_BUILD_ID_H */
