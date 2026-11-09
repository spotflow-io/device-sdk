#include "persistent_buffer/spotflow_persistent_buffer.h"
#include "net/spotflow_processor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_persistent_buffer, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define PERSISTER_STACK_SIZE 2048

static void persister_thread_entry(void);

static K_SEM_DEFINE(persister_wake_sem, 0, 1);

/* The persister runs in its own thread rather than on the processing thread, because the
 * processing thread blocks while the MQTT transport connects and data must be written to flash
 * while offline. Flash erases never run on the Zephyr log thread. */
K_THREAD_DEFINE(spotflow_persister_thread, PERSISTER_STACK_SIZE, persister_thread_entry, NULL,
		NULL, NULL, SPOTFLOW_THREAD_PRIORITY, 0, 0);

void spotflow_pb_wake_persister(void)
{
	k_sem_give(&persister_wake_sem);
}

static void persister_thread_entry(void)
{
	int rc = spotflow_pb_init();

	if (rc < 0) {
		LOG_ERR("Persistent buffer disabled, sending from RAM only: %d", rc);
		return;
	}

	while (true) {
		(void)k_sem_take(&persister_wake_sem,
				 K_MSEC(CONFIG_SPOTFLOW_PERSISTENT_BUFFER_FLUSH_INTERVAL_MS));

		rc = spotflow_pb_persist();
		if (rc < 0) {
			LOG_DBG("Failed to persist queued messages: %d", rc);
		}
	}
}
