#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>
#include <zephyr/net/socket.h>
#include <stdint.h>

#include "net/spotflow_mqtt.h"
#include "net/spotflow_connection_helper.h"
#include "net/spotflow_device_id.h"
#include "net/spotflow_tls.h"

/* 80 bytes is just password itself */
/* should at least match MBEDTLS_SSL_MAX_CONTENT_LEN - default is 4096 */
#define APP_MQTT_BUFFER_SIZE 4096

/* Maximum size of the payload of C2D messages */
#define C2D_PAYLOAD_BUFFER_SIZE 32

#define DEFAULT_GENERAL_TIMEOUT_MSEC 500
#define SPOTFLOW_MQTT_INGEST_CBOR_TOPIC "ingest-cbor"
#define SPOTFLOW_MQTT_CONFIG_CBOR_D2C_TOPIC "config-cbor-d2c"
#define SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC "config-cbor-c2d"

#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define LOG_DBG_PRINT_RESULT(func, rc) LOG_DBG("%s: %d <%s>", (func), rc, RC_STR(rc))

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

struct mqtt_config {
	char* host;
	int port;
	struct zsock_addrinfo* server_addr;
	struct mqtt_utf8 username;
	struct mqtt_utf8 password;
	struct mqtt_utf8 ingest_topic;
	struct mqtt_utf8 config_d2c_topic;
	struct mqtt_utf8 config_c2d_topic;
};

struct mqtt_client_toolset {
	struct mqtt_client mqtt_client;
	struct sockaddr_storage broker;
	struct zsock_pollfd fds[1];
	int nfds;
	bool mqtt_connected;
};

static int client_init(struct mqtt_client* client);
static int poll_with_timeout(int timeout);
static int prepare_fds();
static int spotflow_mqtt_publish_cbor_msg(uint8_t* payload, size_t len, struct mqtt_utf8 topic);
static void mqtt_evt_handler(struct mqtt_client* client, const struct mqtt_evt* evt);
static void clear_fds(void);

static struct mqtt_config spotflow_mqtt_config = {
	.host = CONFIG_SPOTFLOW_SERVER_HOSTNAME,
	.port = CONFIG_SPOTFLOW_SERVER_PORT,
	.server_addr = NULL,
	.username = { 0 },
	.password = MQTT_UTF8_LITERAL(CONFIG_SPOTFLOW_INGEST_KEY),
	.ingest_topic = MQTT_UTF8_LITERAL(SPOTFLOW_MQTT_INGEST_CBOR_TOPIC),
	.config_d2c_topic = MQTT_UTF8_LITERAL(SPOTFLOW_MQTT_CONFIG_CBOR_D2C_TOPIC),
	.config_c2d_topic = MQTT_UTF8_LITERAL(SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC),
};

static struct mqtt_client_toolset mqtt_client_toolset = { .mqtt_connected = false };

/* Buffers for MQTT client. */
static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* Buffer for C2D messages */
static uint8_t c2d_payload_buffer[C2D_PAYLOAD_BUFFER_SIZE];

int spotflow_mqtt_poll()
{
	/* 1) Network I/O: wait up to 10 ms for socket readability */
	int rc = zsock_poll(mqtt_client_toolset.fds, 1, 10);
	/* rc = 0 means time out, negative mean error */
	if (rc < 0) {
		LOG_DBG("zsock_poll() returned error %d,errno: %d", rc, errno);
		return rc;
	} else if (rc == 0) {
		/* no data on socket, continue */
		return 0;
		/* this means that rc is positive -> there is pollfd structures that have selected events */
	} else if (mqtt_client_toolset.fds[0].revents & ZSOCK_POLLIN) {
		/* there's data on the TCP socket—parse it */
		return mqtt_input(&mqtt_client_toolset.mqtt_client);
	} else {
		LOG_DBG("Unexpected poll zsock_poll returned positive but fds nor readable");
		return -EINVAL;
	}
}

void spotflow_mqtt_abort_mqtt()
{
	mqtt_client_toolset.mqtt_connected = false;
	mqtt_abort(&mqtt_client_toolset.mqtt_client);
}

bool spotflow_mqtt_is_connected()
{
	return mqtt_client_toolset.mqtt_connected;
}

int spotflow_mqtt_send_live()
{
	int rc = mqtt_live(&mqtt_client_toolset.mqtt_client);
	if (rc == -EAGAIN) {
		/* no keep-alive needed right now; continue */
		return 0;
	}
	return rc;
}

void spotflow_mqtt_establish_mqtt()
{
	/* infinitely try to connect to mqtt broker */
	while (!mqtt_client_toolset.mqtt_connected) {
		int rc;
		rc = client_init(&mqtt_client_toolset.mqtt_client);
		if (rc != 0) {
			k_sleep(K_MSEC(DEFAULT_GENERAL_TIMEOUT_MSEC));
			continue;
		}

		rc = mqtt_connect(&mqtt_client_toolset.mqtt_client);
		if (rc < 0) {
			LOG_DBG_PRINT_RESULT("mqtt_connect", rc);
			LOG_DBG_PRINT_RESULT("mqtt_connect - errno", errno);
			k_sleep(K_MSEC(DEFAULT_GENERAL_TIMEOUT_MSEC));
			mqtt_abort(&mqtt_client_toolset.mqtt_client);
			continue;
		}

		rc = prepare_fds(&mqtt_client_toolset.mqtt_client);
		if (rc < 0) {
			LOG_ERR("Failed to prepare fds for mqtt client: %d", rc);
			mqtt_abort(&mqtt_client_toolset.mqtt_client);
			/* consider breaking whole loop and exiting
			 * - most likely not recoverable */
			k_sleep(K_MSEC(DEFAULT_GENERAL_TIMEOUT_MSEC));
			continue;
		}

		if (poll_with_timeout(DEFAULT_GENERAL_TIMEOUT_MSEC)) {
			rc = mqtt_input(&mqtt_client_toolset.mqtt_client);
			if (rc < 0) {
				LOG_DBG(" mqtt_input() during CONNACK failed: rc=%d", rc);
				mqtt_abort(&mqtt_client_toolset.mqtt_client);
				k_sleep(K_MSEC(DEFAULT_GENERAL_TIMEOUT_MSEC));
				continue;
			}
		} else {
			LOG_DBG("Poll not success");
			mqtt_abort(&mqtt_client_toolset.mqtt_client);
			k_sleep(K_MSEC(DEFAULT_GENERAL_TIMEOUT_MSEC));
			continue;
		}

		if (!mqtt_client_toolset.mqtt_connected) {
			LOG_DBG("Not connected, aborting!");
			LOG_DBG_PRINT_RESULT("mqtt_connect - not connected", -errno);
			mqtt_abort(&mqtt_client_toolset.mqtt_client);
		}
	}
	LOG_INF("MQTT connected!");
}

static int prepare_fds()
{
	if (mqtt_client_toolset.mqtt_client.transport.type == MQTT_TRANSPORT_SECURE) {
		LOG_DBG("Using secure");
		mqtt_client_toolset.fds[0].fd = mqtt_client_toolset.mqtt_client.transport.tls.sock;
	} else {
		LOG_ERR("Unknown transport type");
		return -EINVAL;
	}

	mqtt_client_toolset.fds[0].events = ZSOCK_POLLIN;
	mqtt_client_toolset.nfds = 1;
	return 0;
}

static int client_init(struct mqtt_client* client)
{
	mqtt_client_init(client);

	LOG_DBG("Resolving DNS");
	int rc = spotflow_conn_helper_resolve_hostname(spotflow_mqtt_config.host,
						       &spotflow_mqtt_config.server_addr);
	if (rc < 0) {
		LOG_ERR("Failed to resolve DNS for %s: %d", CONFIG_SPOTFLOW_SERVER_HOSTNAME, rc);
		return rc;
	}

	spotflow_conn_helper_broker_set_addr_and_port(&mqtt_client_toolset.broker,
						      spotflow_mqtt_config.server_addr,
						      spotflow_mqtt_config.port);

	const char* device_id = spotflow_get_device_id();
	spotflow_mqtt_config.username =
	    (struct mqtt_utf8){ .utf8 = device_id, .size = strlen(device_id) };

	/* MQTT client configuration (client ID is assigned by the broker) */
	client->broker = &mqtt_client_toolset.broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id = MQTT_UTF8_LITERAL("");
	client->password = &spotflow_mqtt_config.password;
	client->user_name = &spotflow_mqtt_config.username;
	client->protocol_version = MQTT_VERSION_3_1_1;
	client->clean_session = true;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	LOG_DBG("Using secure transport");
	client->transport.type = MQTT_TRANSPORT_SECURE;
	struct mqtt_sec_config* tls_config = &client->transport.tls.config;
	spotflow_tls_configure(spotflow_mqtt_config.host, tls_config);

	return 0;
}

static int poll_with_timeout(int timeout)
{
	int ret = 0;

	if (mqtt_client_toolset.nfds > 0) {
		ret = zsock_poll(mqtt_client_toolset.fds, mqtt_client_toolset.nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

int spotflow_mqtt_request_config_subscription()
{
	struct mqtt_topic topics[] = {
		{
		    .topic = spotflow_mqtt_config.config_c2d_topic,
		    .qos = MQTT_QOS_0_AT_MOST_ONCE,
		},
	};

	struct mqtt_subscription_list param = {
		.list = topics,
		.list_count = ARRAY_SIZE(topics),
		.message_id = sys_rand16_get(),
	};

	return mqtt_subscribe(&mqtt_client_toolset.mqtt_client, &param);
}

int spotflow_mqtt_publish_ingest_cbor_msg(uint8_t* payload, size_t len)
{
	return spotflow_mqtt_publish_cbor_msg(payload, len, spotflow_mqtt_config.ingest_topic);
}

int spotflow_mqtt_publish_config_cbor_msg(uint8_t* payload, size_t len)
{
	return spotflow_mqtt_publish_cbor_msg(payload, len, spotflow_mqtt_config.config_d2c_topic);
}

static int spotflow_mqtt_publish_cbor_msg(uint8_t* payload, size_t len, struct mqtt_utf8 topic)
{
	struct mqtt_publish_param param;
	/* using lowest guarantee because handling puback (for better guarantees)
	 * is not implemented now */
	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	param.message.topic.topic = topic;
	param.message.payload.data = payload;
	param.message.payload.len = len;
	param.message_id = sys_rand16_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;
	return mqtt_publish(&mqtt_client_toolset.mqtt_client, &param);
}

static void mqtt_evt_handler(struct mqtt_client* client, const struct mqtt_evt* evt)
{
	int ret;
	switch (evt->type) {
	case MQTT_EVT_SUBACK:
		LOG_DBG("SUBACK packet id: %u", evt->param.suback.message_id);
		break;
	case MQTT_EVT_UNSUBACK:
		LOG_DBG("UNSUBACK packet id: %u", evt->param.suback.message_id);
		break;
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}
		mqtt_client_toolset.mqtt_connected = true;
		LOG_DBG("MQTT client connected!");
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT client disconnected %d", evt->result);
		mqtt_client_toolset.mqtt_connected = false;
		clear_fds();
		break;
	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
		break;
	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}
		LOG_DBG("PUBREC packet id: %u", evt->param.pubrec.message_id);
		const struct mqtt_pubrel_param rel_param = { .message_id =
								 evt->param.pubrec.message_id };
		ret = mqtt_publish_qos2_release(client, &rel_param);
		if (ret < 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", ret);
		}
		break;
	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}
		LOG_DBG("PUBCOMP packet id: %u", evt->param.pubcomp.message_id);
		break;
	case MQTT_EVT_PUBLISH:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBLISH decode error: %d", evt->result);
			break;
		}
		LOG_DBG("PUBLISH packet id: %u", evt->param.publish.message_id);

		ret = mqtt_read_publish_payload(client, c2d_payload_buffer,
						sizeof(c2d_payload_buffer));
		if (ret < 0) {
			LOG_ERR("Failed to read PUBLISH payload: %d", ret);
			break;
		}
		spotflow_mqtt_handle_publish_callback(c2d_payload_buffer, ret);
		break;
	default:
		break;
	}
}

static void clear_fds(void)
{
	mqtt_client_toolset.nfds = 0;
}
