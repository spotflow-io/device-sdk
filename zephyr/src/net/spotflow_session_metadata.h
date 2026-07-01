#ifndef SPOTFLOW_SESSION_METADATA_H
#define SPOTFLOW_SESSION_METADATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_session_metadata_encode(uint8_t* buffer, size_t buffer_len, size_t* cbor_data_len);
int spotflow_session_metadata_send(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_SESSION_METADATA_H */
