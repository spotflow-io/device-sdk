#ifndef SPOTFLOW_LOG_CBOR_H
#define SPOTFLOW_LOG_CBOR_H

#include "spotflow.h"
#include "logging/spotflow_log_backend.h"

uint8_t* log_cbor(char *fem, char* body, uint8_t severity, size_t *out_len, struct message_metadata *metadata);
void log_cbor_send(char *fem, char* buffer, char log_severity, struct message_metadata *metadata);

#endif