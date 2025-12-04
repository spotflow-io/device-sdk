#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_net.h"
#include "logging/spotflow_log_queue.h"

static queue_msg_t msg;

int spotflow_logging_send_message(void)
{
	if (spotflow_queue_read(&msg)) {
		int msg_id =
		    spotflow_mqtt_publish_messgae(SPOTFLOW_MQTT_LOG_TOPIC, msg.ptr, msg.len,
						  SPOTFLOW_MQTT_LOG_QOS // QoS
		    );

		if (msg_id < 0) {
			return msg_id;
		} else {
			spotflow_queue_free(&msg);
			return 0;
		}
	}
	else {
		return SPOTFLOW_MESSAGE_QUEUE_EMPTY; // Queue empty
	}
	return 0;
}