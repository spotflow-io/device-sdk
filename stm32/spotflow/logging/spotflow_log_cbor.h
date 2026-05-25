#ifndef SPOTFLOW_LOG_CBOR_H
#define SPOTFLOW_LOG_CBOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "spotflow_log_backend.h"
/* CBOR encoding functions */
uint8_t* spotflow_log_cbor(const char* log_template,
                           char* body,
                           size_t* out_len,
                           const struct message_metadata* metadata);

void spotflow_log_cbor_send(const char* log_template,
                            char* buffer,
                            const struct message_metadata* metadata);

/* Level conversion helpers */
uint32_t spotflow_cbor_convert_log_level_to_severity(uint8_t lvl);
uint8_t spotflow_cbor_convert_severity_to_log_level(uint32_t severity);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_LOG_CBOR_H */
