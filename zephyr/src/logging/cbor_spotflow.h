#ifndef CBOR_SPOTFLOW_H
#define CBOR_SPOTFLOW_H

#include <stddef.h>
#include <stdint.h>
#include "zephyr/logging/log.h"

#include "cbor_output_context_spotflow.h"

#ifdef __cplusplus
extern "C"
{
#endif



int spotflow_cbor_encode_message(struct log_msg *log_msg,
			struct spotflow_cbor_output_context *output_context,
			uint8_t **cbor_data,
			size_t *cbor_data_len);

#ifdef __cplusplus
}
#endif

#endif /* CBOR_SPOTFLOW_H*/
