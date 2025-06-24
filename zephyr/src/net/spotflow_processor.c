#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "net/spotflow_processor.h"
#include "spotflow_mqtt.h"
#include "spotflow_connection_helper.h"
#include "spotflow_tls.h"

#define APP_CONNECT_TIMEOUT_MS 10000



#define LOG_DBG_PRINT_RESULT(func, rc) \
LOG_DBG("%s: %d <%s>", (func), rc, RC_STR(rc))

LOG_MODULE_REGISTER(spotflow_processor, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

/* https://docs.zephyrproject.org/apidoc/latest/group__msgq__apis.html
 * Alignment of the message queue's ring buffer is not necessary,
 * setting q_align to 1 is sufficient.*/
K_MSGQ_DEFINE(g_spotflow_mqtt_msgq,
		sizeof(struct spotflow_mqtt_msg *),
		CONFIG_SPOTFLOW_LOG_BACKEND_QUEUE_SIZE,
		1);

static struct k_poll_event events[1];

static void mqtt_thread(void);
static void process_mqtt();


static uint32_t messages_sent_counter = 0;

#ifndef CONFIG_SPOTFLOW_MQTT_LOG_THREAD_PRIORITY
#define SPOTFLOW_MQTT_LOG_THREAD_PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO
#else
#define SPOTFLOW_MQTT_LOG_THREAD_PRIORITY CONFIG_SPOTFLOW_MQTT_LOG_THREAD_PRIORITY
#endif

K_THREAD_DEFINE(mqtt_log_thread_id,
		CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE,
		mqtt_thread,
		NULL, NULL, NULL,
		SPOTFLOW_MQTT_LOG_THREAD_PRIORITY,
		0, 0);

void spotflow_start_mqtt(void)
{
	k_thread_start(mqtt_log_thread_id);
	LOG_DBG("Thread started with priority %d and stack size %d",
		SPOTFLOW_MQTT_LOG_THREAD_PRIORITY,
		CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE);
}

static void mqtt_thread(void)
{
	LOG_INF("Starting processing thread");

	wait_for_network();

	spotflow_tls_init();

	LOG_INF("Registered TLS credentials");

	/* 1) OUTER LOOP: keep trying until mqtt_connected == true, reconnect if connection failed */
	while (true)
	{
		spotflow_mqtt_establish_mqtt();

		/* set up k_poll on msgq */
		k_poll_event_init(&events[0],
					K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&g_spotflow_mqtt_msgq);

		process_mqtt();
	}
}


void process_mqtt()
{
	struct spotflow_mqtt_msg *msg_ptr;
	/*  INNER LOOP: perform normal MQTT I/O until an error occurs. */
	while (spotflow_mqtt_is_connected())
	{
		int rc;
		rc = spotflow_mqtt_poll();
		if (rc < 0) {
			LOG_DBG("spotflow_mqtt_poll() returned error %d", rc);
			spotflow_mqtt_abort_mqtt();
			break; /* break out of inner loop; outer loop will reconnect */
		}

		/* 2) Message-queue I/O: check (non-blocking) for enqueued logs */
		k_poll(events, 1, K_NO_WAIT); /* polls events[0] */
		if (events[0].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
		{
			if (k_msgq_get(&g_spotflow_mqtt_msgq, &msg_ptr, K_NO_WAIT) == 0)
			{
				rc = spotflow_mqtt_publish_cbor_log_msg(msg_ptr);
				if (rc < 0)
				{
					LOG_DBG("Failed to publish cbor log message rc: %d -> aborting mqtt connection",rc);
					spotflow_mqtt_abort_mqtt();
					/* Free the message buffer before breaking */
					k_free(msg_ptr);
					break;
				}

				messages_sent_counter++;
				k_free(msg_ptr);
				if (messages_sent_counter % 100 == 0)
				{
					LOG_INF("Sent %" PRIu32 " messages", messages_sent_counter);
				}
				if (messages_sent_counter == UINT32_MAX)
				{
					LOG_INF("Sent %" PRIu32 " messages. Reset.", messages_sent_counter);
					messages_sent_counter = 0; /* reset counter */
				}
			}
			events[0].state = K_POLL_STATE_NOT_READY;
		}
		/* -- Finally, let the MQTT library do any keep‐alive or retry logic. */

		rc = spotflow_mqtt_send_live();
		if (rc == -EAGAIN)
		{
			/* no keep-alive needed right now; continue looping */
		}
		else if (rc < 0)
		{
			LOG_DBG("mqtt_live() returned error %d → reconnecting", rc);
			spotflow_mqtt_abort_mqtt();
			break;
		}
	}
}
