#ifndef LOGGING_SPOTFLOW_CBOR_H
#define LOGGING_SPOTFLOW_CBOR_H

#include <stddef.h>
#include <stdint.h>

#include "spotflow.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t spotflow_cbor_encode_log(uint32_t timestamp_ms,
                                SpotflowLevel level,
                                const char *tag,
                                const char *message,
                                uint8_t *out,
                                size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_SPOTFLOW_CBOR_H */