#include "coredump/spotflow_coredump_net.h"
#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"
#include "coredump/spotflow_coredump_queue.h"

static queue_msg_t msg;

int spotflow_coredump_send_message(void)
{
	if (spotflow_queue_coredump_read(&msg)) {
		int msg_id =
		    spotflow_mqtt_publish_message(SPOTFLOW_MQTT_COREDUMP_TOPIC, msg.ptr, msg.len,
						  SPOTFLOW_MQTT_COREDUMP_QOS // QoS
		    );

		if (msg_id < 0) {
			return msg_id;
		} else {
			spotflow_queue_coredump_free(&msg);
			return msg_id;
		}
	}
	else {
		return SPOTFLOW_MESSAGE_QUEUE_EMPTY; // Queue is empty
	}
	return 0;
}