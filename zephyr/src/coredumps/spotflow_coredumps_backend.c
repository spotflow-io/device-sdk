#include "spotflow_coredumps_backend.h"

#include "spotflow_build_id.h"
#include "coredumps/spotflow_coredumps_cbor.h"
#include "net/spotflow_processor.h"
#include "zephyr/random/random.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/debug/coredump.h>

LOG_MODULE_REGISTER(spotflow_coredump, CONFIG_SPOTFLOW_COREDUMPS_PROCESSING_LOG_LEVEL);

K_MSGQ_DEFINE(g_spotflow_core_dumps_msgq, sizeof(struct spotflow_mqtt_coredumps_msg*),
	      CONFIG_SPOTFLOW_COREDUMPS_BACKEND_QUEUE_SIZE, 1);

/*expected stack size should be under 200B, therefore 1024 should be safe*/
#define STACK_SIZE 1024
/*same priority as mqtt processing thread*/
#define THREAD_PRIO SPOTFLOW_MQTT_THREAD_PRIORITY

static void spotflow_coredumps_thread_entry(void);

K_THREAD_DEFINE(spotflow_coredumps_thread, STACK_SIZE, spotflow_coredumps_thread_entry, NULL, NULL,
		NULL, THREAD_PRIO, 0, 0);

void spotflow_coredump_sent()
{
	int rc = coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	if (rc < 0) {
		LOG_ERR("Failed to erase coredump: %d", rc);
	}
}

struct coredump_info {
	size_t size;
	off_t offset;
	int chunk_ordinal;
	uint32_t coredump_id;
	uint8_t buffer[CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE];
};

static struct coredump_info coredump_info;

/* blocks forever until space is available */
static int enqueue_log_msg(const struct spotflow_mqtt_coredumps_msg* msg)
{
	int rc = k_msgq_put(&g_spotflow_core_dumps_msgq, &msg, K_FOREVER);
	return rc;
}

static void spotflow_coredumps_thread_entry(void)
{
	/* Check if there is a dump */
	int rc = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);
	if (rc < 0) {
		LOG_ERR("Failed to query coredump: %d", rc);
		return;
	}
	if (rc == 0) {
		LOG_DBG("No coredump in flash");
		return;
	}
	if (rc > 1) {
		LOG_ERR("Unknown response code when cheking coredump: %d", rc);
		return;
	}
	LOG_DBG("Coredump found in flash, starting processing thread");

	k_thread_start(spotflow_coredumps_thread);
	/* Get the size of dump */
	int dump_size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (dump_size <= 0) {
		LOG_ERR("Invalid dump size %d", dump_size);
		return;
	}

	coredump_info.size = dump_size;
	coredump_info.offset = 0;
	coredump_info.chunk_ordinal = 0;
	coredump_info.coredump_id = sys_rand32_get();

	LOG_DBG("Starting core dump processing with ID: %" PRIu32 " with size: %d",
		coredump_info.coredump_id, dump_size);

	while (coredump_info.offset < coredump_info.size) {
		size_t remaining_size = coredump_info.size - coredump_info.offset;
		size_t chunk_length;
		if (remaining_size >= CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE) {
			chunk_length = CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE;
		} else {
			chunk_length = remaining_size;
		}

		struct coredump_cmd_copy_arg arg = {
			.offset = coredump_info.offset,
			.buffer = coredump_info.buffer,
			.length = chunk_length,
		};

		int copied = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg);
		if (copied < 0) {
			LOG_ERR("Failed to copy dump (%d)", copied);
			return;
		}
		if (copied != chunk_length) {
			LOG_ERR("Incorrect chunk size copied: expected %d, got %d", chunk_length,
				copied);
			return;
		}

		bool is_last_chunk = (coredump_info.offset + copied) >= coredump_info.size;
		if (is_last_chunk) {
			LOG_DBG("Processing last chunk of coredump");
		}

		const uint8_t* build_id = NULL;
		uint16_t build_id_len = 0;

#ifdef CONFIG_SPOTFLOW_GENERATE_BUILD_ID
		/* Only the first chunk contains the build ID */
		if (coredump_info.chunk_ordinal == 0) {
			int build_id_rc = spotflow_build_id_get(&build_id, &build_id_len);
			if (build_id_rc != 0) {
				LOG_DBG("Failed to get build ID for core dump: %d", build_id_rc);
			}
		}
#endif

		uint8_t* cbor_data = NULL;
		size_t cbor_data_len = 0;
		rc = spotflow_cbor_encode_coredump(
		    coredump_info.buffer, copied, coredump_info.chunk_ordinal,
		    coredump_info.coredump_id, is_last_chunk, build_id, build_id_len, &cbor_data,
		    &cbor_data_len);

		if (rc < 0) {
			LOG_DBG("Failed to encode core dump message: %d", rc);
			return;
		}

		/* Fill the queue with a full chunk */
		struct spotflow_mqtt_coredumps_msg* msg =
		    k_malloc(sizeof(struct spotflow_mqtt_coredumps_msg));
		if (!msg) {
			LOG_DBG("Failed to allocate memory for message");
			k_free(cbor_data);
			return;
		}

		/* Set up the message */
		msg->payload = cbor_data;
		msg->len = cbor_data_len;
		msg->coredump_last_chunk = is_last_chunk;

		rc = enqueue_log_msg(msg);
		if (rc < 0) {
			LOG_ERR("Failed to enqueue coredump chunk: %d", rc);
			k_free(cbor_data);
			k_free(msg);
			return;
		}
		coredump_info.chunk_ordinal++;
		coredump_info.offset += copied;
	}
	LOG_INF("All coredump chunks enqueued");
}
