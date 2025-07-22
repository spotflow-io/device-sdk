#include "coredumps/spotflow_cbor.h"
#include "net/spotflow_processor.h"
#include "zephyr/random/random.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/debug/coredump.h>

LOG_MODULE_REGISTER(spotflow_coredump_backend, CONFIG_SPOTFLOW_PROCESSING_BACKEND_COREDUMPS_LEVEL);

K_MSGQ_DEFINE(g_spotflow_core_dumps_msgq, sizeof(struct spotflow_mqtt_msg*),
	      CONFIG_SPOTFLOW_LOG_BACKEND_QUEUE_SIZE, 1);

/* thread state */
/*todo stack size*/
#define STACK_SIZE 1024
/*todo parametrize thread priority*/
#define THREAD_PRIO K_LOWEST_APPLICATION_THREAD_PRIO

static K_THREAD_STACK_DEFINE(spotflow_thread_stack, STACK_SIZE);
static struct k_thread spotflow_thread_data;

static struct coredump_info {
	size_t size;
	off_t offset;
	int chunk_ordinal;
	uint32_t coredump_id;
	uint8_t buffer[CONFIG_SPOTFLOW_COREDUMP_CHUNK_SIZE];
};

static struct k_work fill_work;
static struct coredump_info coredump_info;

/* blocks forever until space is available */
static int enqueue_log_msg(const struct spotflow_mqtt_msg* msg)
{
	int rc = k_msgq_put(&g_spotflow_core_dumps_msgq, &msg, K_FOREVER);
	return rc;
}

static void spotflow_coredump_thread(struct k_work* fill_work)
{
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

	while (coredump_info.offset < coredump_info.size) {
		size_t remaining_size = coredump_info.size - coredump_info.offset;
		size_t chunk_length;
		if (remaining_size >= CONFIG_SPOTFLOW_COREDUMP_CHUNK_SIZE) {
			chunk_length = CONFIG_SPOTFLOW_COREDUMP_CHUNK_SIZE;
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

		uint8_t* cbor_data = NULL;
		size_t cbor_data_len = 0;
		int rc = spotflow_cbor_encode_coredump(
		    coredump_info.buffer, copied, coredump_info.chunk_ordinal,
		    coredump_info.coredump_id, is_last_chunk, &cbor_data, &cbor_data_len);

		if (rc < 0) {
			LOG_DBG("Failed to encode core dump message: %d", rc);
			return;
		}

		/* Fill the queue with a full chunk */
		struct spotflow_mqtt_msg* msg = k_malloc(sizeof(struct spotflow_mqtt_msg));
		if (!msg) {
			LOG_DBG("Failed to allocate memory for message");
			k_free(cbor_data);
			return;
		}

		/* Set up the message */
		msg->payload = cbor_data;
		msg->len = cbor_data_len;

		rc = enqueue_log_msg(msg);
		if (rc < 0) {
			LOG_ERR("Failed to enqueue coredump chunk: %d", rc);
			k_free(cbor_data);
			k_free(msg);
			return;
		}
		coredump_info.chunk_ordinal++;
		coredump_info.offset += copied;

		LOG_DBG("Enqueued coredump chunk of size %zu", copied);
	}
	LOG_INF("coredump thread done");
	/* Erase the stored dump now that it's queued */
	coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
}

void init_core_dumps()
{
	LOG_DBG("Processing existing coredump...");
	/* Check if there is a dump */
	int has = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);
	if (has <= 0) {
		LOG_INF("No coredump in flash (or error %d)", has);
		return;
	}

	k_thread_create(&spotflow_thread_data, spotflow_thread_stack, STACK_SIZE,
			spotflow_coredump_thread, NULL, NULL, NULL, THREAD_PRIO, 0, K_NO_WAIT);
}

void trigger_queue_fill()
{
	LOG_DBG("triggering queue fill");
	k_work_submit(&fill_work);
}

/*todo we use optimistic approach - device reboot after all chunks fit into buffer but before
 * sending, chunks might be lost.
 */
void process_existing_coredump()
{
	/* This function is a placeholder for processing existing coredumps.
	 * It can be implemented to read from a storage medium, analyze the
	 * coredump data, or perform any other necessary operations.
	 */

	/*TODO send initial empty chunk */

	/* Allocate a buffer and copy it off flash */
	uint8_t* buf = k_malloc(dump_size);
	if (!buf) {
		LOG_ERR("OOM allocating %d bytes", dump_size);
		return;
	}
	struct coredump_cmd_copy_arg arg = {
		.offset = 0,
		.buffer = buf,
		.length = dump_size,
	};
	int copied = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg);
	if (copied != dump_size) {
		LOG_ERR("Failed to copy dump (%d)", copied);
		k_free(buf);
		return;
	}

	/* 4. Send it out — here we use a hex‑dump over the log backend */
	for (int off = 0; off < dump_size; off += 16) {
		size_t chunk = MIN(16, dump_size - off);
		LOG_HEXDUMP_INF(buf + off, chunk, "CoreDump");
	}

	/* 5. Erase it so next crash can be saved */
	coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);

	k_free(buf);
}
