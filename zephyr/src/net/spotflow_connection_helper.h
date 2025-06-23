#ifndef SPOTFLOW_CONNECTION_HELPER_H
#define SPOTFLOW_CONNECTION_HELPER_H

#include <zephyr/net/mqtt.h>

#ifdef __cplusplus
extern "C"
{
#endif

int spotflow_conn_helper_resolve_hostname(const char *hostname,
					struct zsock_addrinfo **server_addr);

void spotflow_conn_helper_broker_set_addr_and_port(struct sockaddr_storage *broker,
						const struct zsock_addrinfo *server_addr,
						int port);

void wait_for_network();

#ifdef __cplusplus
}
#endif

#endif /*SPOTFLOW_CONNECTION_HELPER_H*/
