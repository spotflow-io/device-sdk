#ifndef SPOTFLOW_COREDUMPS_CBOR_H
#define SPOTFLOW_COREDUMPS_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_cbor_encode_coredump(const uint8_t* coredump_data, size_t coredump_data_len,
				  int chunk_ordinal, uint32_t core_dump_id, bool last_chunk,
				  const uint8_t* build_id, size_t build_id_len, uint8_t** cbor_data,
				  size_t* cbor_data_len);
#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_COREDUMPS_CBOR_H*/
