#ifndef SPOTFLOW_CBOR_H
#define SPOTFLOW_CBOR_H

#include <stddef.h>
#include <stdint.h>
#include "zephyr/logging/log.h"

#include "spotflow_cbor_output_context.h"

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_cbor_encode_log(struct log_msg* log_msg, size_t sequence_number,
			     struct spotflow_cbor_output_context* output_context,
			     uint8_t** cbor_data, size_t* cbor_data_len);

uint32_t spotflow_cbor_convert_log_level_to_severity(uint8_t lvl);
uint8_t spotflow_cbor_convert_severity_to_log_level(uint32_t severity);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CBOR_H*/
