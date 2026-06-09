#ifndef SPOTFLOW_BUILD_ID_H
#define SPOTFLOW_BUILD_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_BUILD_ID_LENGTH 20

int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len);

void spotflow_build_id_set_test_override(const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

void spotflow_build_id_clear_test_override(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_BUILD_ID_H */
