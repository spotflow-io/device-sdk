#ifndef SPOTFLOW_CBOR_OUTPUT_CONTEXT_H
#define SPOTFLOW_CBOR_OUTPUT_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_cbor_output_context {
	char cbor_buf[CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN];
	size_t cbor_len;
	char log_msg[CONFIG_SPOTFLOW_LOG_BUFFER_SIZE];
	size_t log_msg_ctr;
};

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CBOR_OUTPUT_CONTEXT_H*/
