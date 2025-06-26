#ifndef SPOTFLOW_CBOR_H
#define SPOTFLOW_CBOR_H

#include <stddef.h>
#include <stdint.h>
#include "zephyr/logging/log.h"

#include "spotflow_cbor_output_context.h"

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_cbor_encode_message(struct log_msg* log_msg,
				 size_t sequence_number,
				 struct spotflow_cbor_output_context* output_context,
				 uint8_t** cbor_data, size_t* cbor_data_len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CBOR_H*/
