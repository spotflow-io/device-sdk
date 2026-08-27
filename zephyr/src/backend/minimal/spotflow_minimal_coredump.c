#include "backend/minimal/spotflow_minimal_coredump.h"

#include "net/spotflow_transport.h"
#include "spotflow_build_id.h"

#include <errno.h>
#include <stdbool.h>
#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>

LOG_MODULE_REGISTER(spotflow_coredump, CONFIG_SPOTFLOW_COREDUMPS_PROCESSING_LOG_LEVEL);

#define MAX_KEY_COUNT 9

#define KEY_MESSAGE_TYPE 0x00
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_COREDUMP_ID 0x09
#define KEY_CHUNK_ORDINAL 0x0A
#define KEY_CONTENT 0x0B
#define KEY_IS_LAST_CHUNK 0x0C
#define KEY_BUILD_ID 0x0E
#define KEY_OS 0x0F

#define COREDUMP_CHUNK_MESSAGE_TYPE 2
#define ZEPHYR_OS_VALUE 1

enum coredump_state {
	COREDUMP_STATE_UNINITIALIZED,
	COREDUMP_STATE_ACTIVE,
	COREDUMP_STATE_ERASE_PENDING,
	COREDUMP_STATE_DONE,
};

static struct {
	enum coredump_state state;
	size_t size;
	off_t offset;
	uint32_t ordinal;
	uint32_t id;
	uint8_t chunk[CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE];
	size_t chunk_len;
	int64_t chunk_uptime_ms;
	const uint8_t* chunk_build_id;
	uint16_t chunk_build_id_len;
	bool chunk_loaded;
} coredump_state;

static int initialize_coredump(void)
{
	int rc = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);

	if (rc < 0) {
		LOG_ERR("Failed to query coredump: %d", rc);
		return rc;
	}
	if (rc == 0) {
		coredump_state.state = COREDUMP_STATE_DONE;
		return 0;
	}
	if (rc != 1) {
		LOG_ERR("Unknown coredump query response: %d", rc);
		return -EINVAL;
	}

	rc = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (rc <= 0) {
		LOG_ERR("Invalid coredump size: %d", rc);
		return rc < 0 ? rc : -EINVAL;
	}

	coredump_state.size = (size_t)rc;
	coredump_state.offset = 0;
	coredump_state.ordinal = 0;
	coredump_state.id = sys_rand32_get();
	coredump_state.chunk_loaded = false;
	coredump_state.state = COREDUMP_STATE_ACTIVE;

	return 0;
}

static int load_chunk(void)
{
	size_t remaining = coredump_state.size - (size_t)coredump_state.offset;
	size_t length = MIN(remaining, sizeof(coredump_state.chunk));
	struct coredump_cmd_copy_arg arg = {
		.offset = coredump_state.offset,
		.buffer = coredump_state.chunk,
		.length = length,
	};
	int copied = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg);

	if (copied < 0) {
		LOG_ERR("Failed to copy coredump: %d", copied);
		return copied;
	}

#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_IN_MEMORY
	copied = (int)length;
#else
	if ((size_t)copied != length) {
		LOG_ERR("Incorrect coredump chunk size: expected %zu, got %d", length, copied);
		return -EIO;
	}
#endif

	coredump_state.chunk_len = (size_t)copied;
	coredump_state.chunk_uptime_ms = k_uptime_get();
	coredump_state.chunk_build_id = NULL;
	coredump_state.chunk_build_id_len = 0;
#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
	if (coredump_state.ordinal == 0) {
		int rc = spotflow_build_id_get(&coredump_state.chunk_build_id,
					       &coredump_state.chunk_build_id_len);

		if (rc != 0) {
			LOG_DBG("Failed to get build ID for coredump: %d", rc);
			coredump_state.chunk_build_id = NULL;
			coredump_state.chunk_build_id_len = 0;
		}
	}
#endif
	coredump_state.chunk_loaded = true;

	return 0;
}

static int encode_chunk(uint8_t* workspace, size_t workspace_size, size_t* encoded_size)
{
	bool last = (size_t)coredump_state.offset + coredump_state.chunk_len >= coredump_state.size;
	bool success;

	ZCBOR_STATE_E(state, 1, workspace, workspace_size, 1);

	success = zcbor_map_start_encode(state, MAX_KEY_COUNT);
	success = success && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, COREDUMP_CHUNK_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, KEY_COREDUMP_ID);
	success = success && zcbor_uint32_put(state, coredump_state.id);
	success = success && zcbor_uint32_put(state, KEY_CHUNK_ORDINAL);
	success = success && zcbor_uint32_put(state, coredump_state.ordinal);
	success = success && zcbor_uint32_put(state, KEY_CONTENT);
	success = success &&
		zcbor_bstr_encode_ptr(state, coredump_state.chunk, coredump_state.chunk_len);
	success = success && zcbor_uint32_put(state, KEY_IS_LAST_CHUNK);
	success = success && zcbor_bool_put(state, last);
	if (coredump_state.chunk_build_id != NULL) {
		success = success && zcbor_uint32_put(state, KEY_BUILD_ID);
		success = success &&
			zcbor_bstr_encode_ptr(state, coredump_state.chunk_build_id,
					      coredump_state.chunk_build_id_len);
	}
	success = success && zcbor_uint32_put(state, KEY_OS);
	success = success && zcbor_uint32_put(state, ZEPHYR_OS_VALUE);
	success = success && zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
	success = success && zcbor_int64_put(state, coredump_state.chunk_uptime_ms);
	success = success && zcbor_map_end_encode(state, MAX_KEY_COUNT);
	if (!success) {
		LOG_DBG("Failed to encode coredump: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*encoded_size = (size_t)(state->payload - workspace);
	return 0;
}

void spotflow_coredump_sent(void)
{
	int rc;

	coredump_state.state = COREDUMP_STATE_ERASE_PENDING;
	coredump_state.chunk_loaded = false;
	rc = coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	if (rc < 0) {
		LOG_ERR("Failed to erase coredump: %d", rc);
		return;
	}
	coredump_state.state = COREDUMP_STATE_DONE;
}

int spotflow_minimal_coredump_process(uint8_t* workspace, size_t workspace_size)
{
	size_t encoded_size;
	bool last;
	int rc;

	if (workspace == NULL || workspace_size == 0) {
		return -EINVAL;
	}

	if (coredump_state.state == COREDUMP_STATE_UNINITIALIZED) {
		rc = initialize_coredump();
		if (rc < 0 || coredump_state.state == COREDUMP_STATE_DONE) {
			return rc;
		}
	}

	if (coredump_state.state == COREDUMP_STATE_DONE) {
		return 0;
	}
	if (coredump_state.state == COREDUMP_STATE_ERASE_PENDING) {
		rc = coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
		if (rc < 0) {
			LOG_ERR("Failed to erase coredump: %d", rc);
			return rc;
		}
		coredump_state.state = COREDUMP_STATE_DONE;
		return 1;
	}

	if (!coredump_state.chunk_loaded) {
		rc = load_chunk();
		if (rc < 0) {
			return rc;
		}
	}

	rc = encode_chunk(workspace, workspace_size, &encoded_size);
	if (rc < 0) {
		return rc;
	}

	rc = spotflow_transport_send_ingest_cbor(workspace, encoded_size);
	if (rc == -EAGAIN) {
		return rc;
	}
	if (rc < 0) {
		LOG_DBG("Failed to publish coredump: %d, aborting connection", rc);
		spotflow_transport_abort();
		return rc;
	}

	last = (size_t)coredump_state.offset + coredump_state.chunk_len >= coredump_state.size;
	coredump_state.offset += (off_t)coredump_state.chunk_len;
	coredump_state.ordinal++;
	coredump_state.chunk_loaded = false;
	if (last) {
		LOG_INF("Coredump successfully sent.");
		spotflow_coredump_sent();
	}

	return 1;
}
