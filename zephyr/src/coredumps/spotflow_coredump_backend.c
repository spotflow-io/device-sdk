#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/debug/coredump.h>


LOG_MODULE_REGISTER(spotflow_coredump_backend, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

K_MSGQ_DEFINE(g_spotflow_core_dumps_msgq, sizeof(struct spotflow_mqtt_msg*),
	      CONFIG_SPOTFLOW_LOG_BACKEND_QUEUE_SIZE, 1);

#ifdef CONFIG_SPOTFLOW_CORE_DUMPS
/*Triggering init routine on start to prepare first chunks to the core dump queue*/

#endif /*CONFIG_SPOTFLOW_CORE_DUMPS*/

/*todo we use optimistic approach - device reboot after all chunks fit into buffer but before
 * sending, chunks might be lost.
 */
void process_existing_coredump()
{
	/* This function is a placeholder for processing existing coredumps.
	 * It can be implemented to read from a storage medium, analyze the
	 * coredump data, or perform any other necessary operations.
	 */
	LOG_INF("Processing existing coredump...");
	/* Check if there is a dump */
	int has = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);
	if (has <= 0) {
		LOG_INF("No coredump in flash (or error %d)", has);
		return;
	}
	/*TODO send initial empty chunk */

	/* Get the size of dump */
	int dump_size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (dump_size <= 0) {
		LOG_ERR("Invalid dump size %d", dump_size);
		return;
	}

	/* Allocate a buffer and copy it off flash */
	uint8_t *buf = k_malloc(dump_size);
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
