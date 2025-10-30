#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/socket.h>
#include <version_helper.h>

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define L4_EVENT_MASK \
	(NET_EVENT_DNS_SERVER_ADD | NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

/* To provide backward compatibility for zephyr <4.2.0 */
#if SPOTFLOW_ZEPHYR_VERSION_GE(4, 2)
typedef uint64_t mgmt_evt_t;
#else
typedef uint32_t mgmt_evt_t;
#endif

static struct net_mgmt_event_callback net_mgmt_event_cb;
static K_SEM_DEFINE(network_connected, 0, 1);

static void l4_event_handler(struct net_mgmt_event_callback* cb, mgmt_evt_t mgmt_event,
			     struct net_if* iface);

/* Query IP address for the broker URL */
int spotflow_conn_helper_resolve_hostname(const char* hostname, struct zsock_addrinfo** server_addr)
{
	int rc;
	struct zsock_addrinfo hints = { .ai_family = AF_INET,
					.ai_socktype = SOCK_STREAM,
					.ai_protocol = 0 };

	if (*server_addr != NULL) {
		zsock_freeaddrinfo(*server_addr);
		*server_addr = NULL;
	}

	rc = zsock_getaddrinfo(hostname, STRINGIFY(CONFIG_SPOTFLOW_SERVER_PORT), &hints,
			       server_addr);
	if (rc < 0) {
		LOG_DBG("DNS not resolved for %s:%d", hostname, CONFIG_SPOTFLOW_SERVER_PORT);
	} else {
		LOG_DBG("DNS%s resolved for %s:%d", "", hostname, CONFIG_SPOTFLOW_SERVER_PORT);
	}
	return rc;
}

void spotflow_conn_helper_broker_set_addr_and_port(struct sockaddr_storage* broker,
						   const struct zsock_addrinfo* server_addr,
						   int port)
{
	struct sockaddr_in* broker4 = (struct sockaddr_in*)broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(port);

	net_ipaddr_copy(&broker4->sin_addr, &net_sin(server_addr->ai_addr)->sin_addr);
}

void wait_for_network()
{
	net_mgmt_init_event_callback(&net_mgmt_event_cb, l4_event_handler, L4_EVENT_MASK);
	net_mgmt_add_event_callback(&net_mgmt_event_cb);

	LOG_INF("Waiting for network...");

	k_sem_take(&network_connected, K_FOREVER);
}

static void l4_event_handler(struct net_mgmt_event_callback* cb, mgmt_evt_t mgmt_event,
			     struct net_if* iface)
{
	switch (mgmt_event) {
	case NET_EVENT_L4_CONNECTED:
		LOG_DBG("Network connectivity established and IP address assigned");
		k_sem_give(&network_connected);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		break;
	default:
		break;
	}
}
