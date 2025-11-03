#include "spotflow.h"
#include "net/spotflow_mqtt.h"
#include "coredump/spotflow_coredump_cbor.h"
#include "logging/spotflow_log_backend.h"
#include "cbor.h"

static const char* TAG = "SPOTFLOW_COREDUMP";

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
#define ESP_IDF_OS_VALUE 2

/* Should be approximately 47 bytes (including build ID), putting 64 to be safe */
#define COREDUMPS_OVERHEAD 64

uint8_t buffer[CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + COREDUMPS_OVERHEAD];

/**
 * @brief Print Cbor hex for debugging purposes
 * 
 * @param buf 
 * @param len 
 */
void print_cbor_hex(const uint8_t* buf, size_t len)
{
	SPOTFLOW_LOG("CBOR buffer (%zu bytes):\n", len);
	for (size_t i = 0; i < len; i++) {
		printf("%02X ", buf[i]); // print each byte as 2-digit hex
		if ((i + 1) % 16 == 0) // 16 bytes per line
			printf("\n");
	}
	printf("\n");
}

/**
 * @brief Encode the cbor
 * 
 * @param coredump_data Core dump data chunk
 * @param coredump_data_len Core dump data chunk length 
 * @param chunk_ordinal Chunk value
 * @param core_dump_id Core dump ID
 * @param last_chunk Whether it's the last chunk or not
 * @param build_id_data build ID data
 * @param build_id_data_len Build ID length
 * @param cbor_data Return pointers for cbor encoded data
 * @param cbor_data_len return pointer for cbor data length
 * @return int 
 */
int spotflow_cbor_encode_coredump(const uint8_t* coredump_data, size_t coredump_data_len,
				  const int chunk_ordinal, const uint32_t core_dump_id, const bool last_chunk,
				  const uint8_t* build_id_data, size_t build_id_data_len,
				  uint8_t** cbor_data, size_t* cbor_data_len)
{
	CborEncoder array_encoder;
	CborEncoder map_encoder;
	uint8_t* buf = malloc(CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + COREDUMPS_OVERHEAD);
	if (!buf) {
		SPOTFLOW_LOG("Failed to allocate CBOR buffer");
		return -1;
	}
	cbor_encoder_init(&array_encoder, buf,
			  CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + COREDUMPS_OVERHEAD, 0);

#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	cbor_encoder_create_map(&array_encoder, &map_encoder, 8); // { If build Id present set the map values to 8 i.e. 8 entries
#else
	cbor_encoder_create_map(&array_encoder, &map_encoder, 7); // Otherwise 7
#endif

	/* start outer map */

	cbor_encode_uint(&map_encoder, KEY_MESSAGE_TYPE);
	cbor_encode_uint(&map_encoder, CORE_DUMP_CHUNK_MESSAGE_TYPE);

	cbor_encode_uint(&map_encoder, KEY_COREDUMP_ID);
	cbor_encode_uint(&map_encoder, core_dump_id);

	cbor_encode_uint(&map_encoder, KEY_CHUNK_ORDINAL);
	cbor_encode_uint(&map_encoder, chunk_ordinal);

	cbor_encode_uint(&map_encoder, KEY_CONTENT);
	cbor_encode_byte_string(&map_encoder, coredump_data, coredump_data_len);

	cbor_encode_uint(&map_encoder, KEY_IS_LAST_CHUNK);
	cbor_encode_boolean(&map_encoder, last_chunk);

	#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	if (build_id_data != NULL) {	//check if due to some reason build ID was not read or set properly
		cbor_encode_uint(&map_encoder, KEY_BUILD_ID);
		cbor_encode_byte_string(&map_encoder, build_id_data, build_id_data_len);
	}
#endif
	cbor_encode_uint(&map_encoder, KEY_OS);
	cbor_encode_uint(&map_encoder, ESP_IDF_OS_VALUE);

	/* finish cbor */
	cbor_encoder_close_container(&array_encoder, &map_encoder); // }

	*cbor_data_len = cbor_encoder_get_buffer_size(&array_encoder, buf);
	*cbor_data = buf;
	print_cbor_hex(buf, *cbor_data_len);
	return 0;
}