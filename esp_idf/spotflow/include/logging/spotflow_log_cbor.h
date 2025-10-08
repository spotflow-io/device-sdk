#ifndef SPOTFLOW_LOG_CBOR_H
#define SPOTFLOW_LOG_CBOR_H

#include "spotflow.h"

uint8_t* log_cbor(char* body, uint8_t severity, size_t *out_len);
void log_cbor_send(char* buffer);

#endif