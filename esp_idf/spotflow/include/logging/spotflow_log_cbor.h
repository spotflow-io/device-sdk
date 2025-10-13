#ifndef SPOTFLOW_LOG_CBOR_H
#define SPOTFLOW_LOG_CBOR_H

#include "spotflow.h"
#include "logging/spotflow_log_backend.h"

uint8_t* log_cbor(const char *fem, char* body, const uint8_t severity, size_t *out_len, const struct message_metadata *metadata);
void log_cbor_send(const char *fem, char* buffer, const char log_severity, const struct message_metadata *metadata);

#endif