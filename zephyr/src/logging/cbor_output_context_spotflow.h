#ifndef CBOR_OUTPUT_CONTEXT_SPOTFLOW_H
#define CBOR_OUTPUT_CONTEXT_SPOTFLOW_H

#include <stdint.h>
#include "zephyr/kernel.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct spotflow_cbor_output_context {
	char cbor_buf[CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN];
	size_t cbor_len;
	char log_msg[CONFIG_SPOTFLOW_LOG_BUFFER_SIZE];
	size_t log_msg_ctr;
};

int spotflow_cbor_output_context_init(struct spotflow_cbor_output_context **_context);

/*not used because output context is used for the whole lifetime of the application*/
void spotflow_cbor_output_context_free(struct spotflow_cbor_output_context *context);

#ifdef __cplusplus
}
#endif

#endif /* CBOR_OUTPUT_CONTEXT_SPOTFLOW_H*/
