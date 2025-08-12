
#include "zcbor_common.h"
#include "zcbor_encode.h"
#include "zephyr/kernel.h"
#include "zephyr/sys/__assert.h"
#include "zephyr/logging/log.h"

LOG_MODULE_DECLARE(spotflow_coredump, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

#define MAX_KEY_COUNT 8

#define KEY_MESSAGE_TYPE 0x00
#define KEY_COREDUMP_ID 0x09
#define KEY_CHUNK_ORDINAL 0x0A
#define KEY_CONTENT 0x0B
#define KEY_IS_LAST_CHUNK 0x0C
#define KEY_BUILD_ID 0x0E
#define KEY_OS 0x0F
/*not used in current version*/
#define KEY_OS_VERSION 0x10

#define CORE_DUMP_CHUNK_MESSAGE_TYPE 2
#define ZEPHYR_OS_VALUE 1

/* Should be approximately 47 bytes (including build ID), putting 64 to be safe */
#define ZCBOR_COREDUMPS_OVERHEAD 64

uint8_t buffer[CONFIG_SPOTFLOW_COREDUMP_CHUNK_SIZE + ZCBOR_COREDUMPS_OVERHEAD];

int spotflow_cbor_encode_coredump(const uint8_t* coredump_data, size_t coredump_data_len,
				  int chunk_ordinal, uint32_t core_dump_id, bool last_chunk,
				  const uint8_t* build_id_data, size_t build_id_data_len,
				  uint8_t** cbor_data, size_t* cbor_data_len)
{
	__ASSERT(coredump_data != NULL, "coredump_data is NULL");
	__ASSERT(coredump_data_len > 0, "coredump_data_len is 0");

	ZCBOR_STATE_E(state, 1, buffer, sizeof(buffer), 1);

	bool succ;
	/* start outer map */
	succ = zcbor_map_start_encode(state, MAX_KEY_COUNT);

	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, CORE_DUMP_CHUNK_MESSAGE_TYPE);

	succ = succ && zcbor_uint32_put(state, KEY_COREDUMP_ID);
	succ = succ && zcbor_uint32_put(state, core_dump_id);

	succ = succ && zcbor_uint32_put(state, KEY_CHUNK_ORDINAL);
	succ = succ && zcbor_uint32_put(state, chunk_ordinal);

	succ = succ && zcbor_uint32_put(state, KEY_CONTENT);
	succ = succ && zcbor_bstr_encode_ptr(state, coredump_data, coredump_data_len);

	succ = succ && zcbor_uint32_put(state, KEY_IS_LAST_CHUNK);
	succ = succ && zcbor_bool_put(state, last_chunk);

	if (build_id_data != NULL) {
		succ = succ && zcbor_uint32_put(state, KEY_BUILD_ID);
		succ = succ && zcbor_bstr_encode_ptr(state, build_id_data, build_id_data_len);
	}

	succ = succ && zcbor_uint32_put(state, KEY_OS);
	succ = succ && zcbor_uint32_put(state, ZEPHYR_OS_VALUE);

	/* finish cbor */
	succ = succ && zcbor_map_end_encode(state, MAX_KEY_COUNT);
	if (succ != true) {
		LOG_DBG("Failed to encode cbor: %d", zcbor_peek_error(state));
		LOG_DBG("Encoding failed: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	/* calculate encoded length */
	*cbor_data_len = state->payload - buffer;
	uint8_t* data = k_malloc(*cbor_data_len);
	if (!data) {
		LOG_DBG("Failed to allocate memory for CBOR data");
		return -ENOMEM;
	}
	/* Copy CBOR data */
	memcpy(data, buffer, *cbor_data_len);

	*cbor_data = data;
	return 0;
}
