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
static void print_cbor_hex(const uint8_t* buf, size_t len)
{
	SPOTFLOW_LOG("CBOR buffer (%zu bytes):\n", len);
	for (size_t i = 0; i < len; i++) {
		printf("%02X ", buf[i]); // print each byte as 2-digit hex
		if ((i + 1) % 16 == 0) // 16 bytes per line
			printf("\n");
	}
	printf("\n");
}

// --- Simple Base64 encoder ---
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
	* @brief helper function for debugging Coredumps CBOR encodings for displaying over terminal in base64 encoding
	* 
	* @param data Cbor
	* @param len Size of Cbor
	*/
static void print_cbor_base64(const uint8_t* data, size_t len)
{
	size_t out_len = 4 * ((len + 2) / 3);
	char* out = malloc(out_len + 1);
	if (!out) {
		SPOTFLOW_LOG("Failed to allocate Base64 buffer");
		return;
	}

	size_t i = 0, j = 0;
	while (i < len) {
		uint32_t octet_a = i < len ? data[i++] : 0;
		uint32_t octet_b = i < len ? data[i++] : 0;
		uint32_t octet_c = i < len ? data[i++] : 0;

		uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

		out[j++] = base64_table[(triple >> 18) & 0x3F];
		out[j++] = base64_table[(triple >> 12) & 0x3F];
		out[j++] = (i > len + 1) ? '=' : base64_table[(triple >> 6) & 0x3F];
		out[j++] = (i > len) ? '=' : base64_table[triple & 0x3F];
	}
	out[j] = '\0';

	SPOTFLOW_LOG("CBOR Base64 (%zu bytes encoded): %s", len, out);
	free(out);
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
				  const int chunk_ordinal, const uint32_t core_dump_id,
				  const bool last_chunk, const uint8_t* build_id_data,
				  size_t build_id_data_len, uint8_t** cbor_data,
				  size_t* cbor_data_len)
{
	if (!coredump_data || coredump_data_len == 0) {
		SPOTFLOW_LOG("Invalid coredump input");
		return -1;
	}

	size_t buf_size = CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + COREDUMPS_OVERHEAD;
	uint8_t* buf = malloc(buf_size);
	if (!buf) {
		SPOTFLOW_LOG("Failed to allocate CBOR buffer");
		return -1;
	}

	CborEncoder encoder;
	CborEncoder map_encoder;
	CborError err;

	cbor_encoder_init(&encoder, buf, buf_size, 0);
	err = cbor_encoder_create_map(&encoder, &map_encoder, CborIndefiniteLength);
	if (err != CborNoError) {
		SPOTFLOW_LOG("Failed to start CBOR map: %d", err);
		free(buf);
		return -1;
	}

	// Encode key-value pairs one by one
	err = cbor_encode_uint(&map_encoder, KEY_MESSAGE_TYPE);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, CORE_DUMP_CHUNK_MESSAGE_TYPE);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, KEY_COREDUMP_ID);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, core_dump_id);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, KEY_CHUNK_ORDINAL);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, chunk_ordinal);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, KEY_CONTENT);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_byte_string(&map_encoder, coredump_data, coredump_data_len);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, KEY_IS_LAST_CHUNK);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_boolean(&map_encoder, last_chunk);
	if (err != CborNoError)
		goto fail;

#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	if (build_id_data && build_id_data_len > 0) {
		err = cbor_encode_uint(&map_encoder, KEY_BUILD_ID);
		if (err != CborNoError)
			goto fail;
			
		err = cbor_encode_byte_string(&map_encoder, build_id_data, build_id_data_len);
		if (err != CborNoError)
			goto fail;
	}
#endif

	err = cbor_encode_uint(&map_encoder, KEY_OS);
	if (err != CborNoError)
		goto fail;

	err = cbor_encode_uint(&map_encoder, ESP_IDF_OS_VALUE);
	if (err != CborNoError)
		goto fail;

	err = cbor_encoder_close_container(&encoder, &map_encoder);
	if (err != CborNoError)
		goto fail;

	*cbor_data_len = cbor_encoder_get_buffer_size(&encoder, buf);
	*cbor_data = buf;

	// print_cbor_base64(buf, *cbor_data_len);
	return 0;

fail:
	SPOTFLOW_LOG("CBOR encoding failed: %d", err);
	free(buf);
	return -1;
}