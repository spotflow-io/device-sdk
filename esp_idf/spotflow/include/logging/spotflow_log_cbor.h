#ifndef SPOTFLOW_LOG_CBOR_H
#define SPOTFLOW_LOG_CBOR_H

uint8_t* log_cbor(const char *log_template, char* body, const uint8_t severity, size_t *out_len, const struct message_metadata *metadata);
void log_cbor_send(const char *log_template, char* buffer, const char log_severity, const struct message_metadata *metadata);
void print_cbor_hex(const uint8_t *buf, size_t len);

#endif