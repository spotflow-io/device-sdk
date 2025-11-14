#ifndef SPOTFLOW_BUILD_ID_H
#define SPOTFLOW_BUILD_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_build_id_get(const uint8_t** build_id, uint16_t* build_id_len);
void spotflow_build_id_print(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_BUILD_ID_H */