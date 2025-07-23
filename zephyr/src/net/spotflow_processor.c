#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "net/spotflow_processor.h"
#include "spotflow_mqtt.h"
#include "spotflow_connection_helper.h"
#include "spotflow_tls.h"

#ifdef CONFIG_SPOTFLOW_CORE_DUMPS
#include "coredumps/spotflow_coredump_backend.h"
#endif /* CONFIG_SPOTFLOW_CORE_DUMPS */
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "logging/spotflow_log_backend.h"
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

#define APP_CONNECT_TIMEOUT_MS 10000

#define LOG_DBG_PRINT_RESULT(func, rc) LOG_DBG("%s: %d <%s>", (func), rc, RC_STR(rc))

LOG_MODULE_REGISTER(spotflow_processor, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

static struct k_poll_event logs_poll_event;
static struct k_poll_event core_dumps_poll_event;

static void mqtt_thread(void);
static void process_mqtt();
static void init_logs_polling();
static void init_core_dumps_polling();

static uint32_t messages_sent_counter = 0;

#if CONFIG_SPOTFLOW_MQTT_THREAD_CUSTOM_PRIORITY
#define SPOTFLOW_MQTT_THREAD_PRIORITY CONFIG_SPOTFLOW_MQTT_THREAD_PRIORITY
#else
#define SPOTFLOW_MQTT_THREAD_PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO
#endif

K_THREAD_DEFINE(mqtt_log_thread_id, CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE, mqtt_thread, NULL,
		NULL, NULL, SPOTFLOW_MQTT_THREAD_PRIORITY, 0, 0);

void spotflow_start_mqtt(void)
{
	k_thread_start(mqtt_log_thread_id);
	LOG_DBG("Thread started with priority %d and stack size %d", SPOTFLOW_MQTT_THREAD_PRIORITY,
		CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE);
}

static void mqtt_thread(void)
{
	LOG_INF("Starting Spotflow processing thread");

	wait_for_network();

	spotflow_tls_init();

	LOG_DBG("Spotflow registered TLS credentials");

	init_core_dumps_polling();
	init_logs_polling();

	/* 1) OUTER LOOP: keep trying until mqtt_connected == true, reconnect if connection failed */
	while (true) {
		spotflow_mqtt_establish_mqtt();

		process_mqtt();
	}
}

#ifdef CONFIG_SPOTFLOW_CORE_DUMPS
static void init_core_dumps_polling()
{
	/* set up k_poll on msgq */
	k_poll_event_init(&core_dumps_poll_event, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, &g_spotflow_core_dumps_msgq);
}
/**
 * Polls for enqueued coredump chunks and processes them.
 * Returns 0 if no coredumps were sent, >0 if coredumps were sent, <0 on error.
 */
static int poll_and_process_enqueued_coredump_chunks()
{
	struct spotflow_mqtt_msg* msg_ptr;
	int rc;
	/* Message-queue: check (non-blocking) for enqueued logs */
	k_poll(&core_dumps_poll_event, 1, K_NO_WAIT); /* polls events[0] */
	if (core_dumps_poll_event.state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
		/*TODO core dumps are using best afford approach - if connection is dropped when transmitting
		 * core dump chunk will be lost, change to peak
		 */
		if (k_msgq_get(&g_spotflow_core_dumps_msgq, &msg_ptr, K_NO_WAIT) == 0) {
			rc = spotflow_mqtt_publish_cbor_msg(msg_ptr);
			if (rc < 0) {
				LOG_DBG("Failed to publish cbor core dump message rc: %d -> "
					"aborting mqtt connection",
					rc);
				spotflow_mqtt_abort_mqtt();
				/* Free the message buffer before breaking */
				k_free(msg_ptr->payload);
				k_free(msg_ptr);
				return rc;
			}
			LOG_DBG("Sent coredump chunk with size %zu bytes", msg_ptr->len);
			k_free(msg_ptr->payload);
			k_free(msg_ptr);
		}
		core_dumps_poll_event.state = K_POLL_STATE_NOT_READY;
		return 1;
	} else {
		return 0;
	}
}
#else
static inline void init_core_dumps_polling() {}
static inline int poll_and_process_enqueued_coredump_chunks()
{
	return 0;
}
#endif /*CONFIG_SPOTFLOW_CORE_DUMPS*/

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
static void init_logs_polling()
{
	/* set up k_poll on msgq */
	k_poll_event_init(&logs_poll_event, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, &g_spotflow_logs_msgq);
}
static int poll_and_process_enqueued_logs()
{
	struct spotflow_mqtt_msg* msg_ptr;
	int rc;
	/* Message-queue: check (non-blocking) for enqueued logs */
	k_poll(&logs_poll_event, 1, K_NO_WAIT); /* polls events[0] */
	if (logs_poll_event.state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
		/*Logs are using best afford approach - if connection is dropped when transmitting
		 * log message will be lost
		 */
		if (k_msgq_get(&g_spotflow_logs_msgq, &msg_ptr, K_NO_WAIT) == 0) {
			rc = spotflow_mqtt_publish_cbor_msg(msg_ptr);
			if (rc < 0) {
				LOG_DBG("Failed to publish cbor log message rc: %d -> "
					"aborting mqtt connection",
					rc);
				spotflow_mqtt_abort_mqtt();
				/* Free the message buffer before breaking */
				k_free(msg_ptr->payload);
				k_free(msg_ptr);
				return rc;
			}

			messages_sent_counter++;
			k_free(msg_ptr->payload);
			k_free(msg_ptr);
			if (messages_sent_counter % 100 == 0) {
				LOG_INF("Sent %" PRIu32 " messages", messages_sent_counter);
			}
			if (messages_sent_counter == UINT32_MAX) {
				LOG_INF("Sent %" PRIu32 " messages. Reset.", messages_sent_counter);
				messages_sent_counter = 0; /* reset counter */
			}
		}
		logs_poll_event.state = K_POLL_STATE_NOT_READY;
	}
	return 0;
}
#else
static void init_logs_polling() {}
static int poll_and_process_enqueued_logs()
{
	return 0; /* No log queue configured, nothing to process */
}
#endif /*CONFIG_SPOTFLOW_LOG_BACKEND*/

static void process_mqtt()
{
	/*  INNER LOOP: perform normal MQTT I/O until an error occurs. */
	while (spotflow_mqtt_is_connected()) {
		int rc;
		rc = spotflow_mqtt_poll();
		if (rc < 0) {
			LOG_DBG("spotflow_mqtt_poll() returned error %d", rc);
			spotflow_mqtt_abort_mqtt();
			break; /* break out of inner loop; outer loop will reconnect */
		}

		rc = poll_and_process_enqueued_coredump_chunks();
		if (rc < 0) {
			/* Problem in sending, reestablishing MQTT Connection*/
			break;
		}
		if (rc == 0) {
			/*No coredumps to send -> sending logs*/
			rc = poll_and_process_enqueued_logs();
			if (rc < 0) {
				break;
			}
		}
		/* rc > 0 means core dumps were sent
		 *	-> doing mqtt routine and continue with core dumps chunks sending*/

		/* -- Let the MQTT library do any keep‐alive or retry logic. */
		rc = spotflow_mqtt_send_live();
		if (rc == -EAGAIN) {
			/* no keep-alive needed right now; continue looping */
		} else if (rc < 0) {
			LOG_DBG("mqtt_live() returned error %d → reconnecting", rc);
			spotflow_mqtt_abort_mqtt();
			break;
		}
	}
}
