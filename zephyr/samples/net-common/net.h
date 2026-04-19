#ifndef SPOTFLOW_SAMPLE_NET_H
#define SPOTFLOW_SAMPLE_NET_H

/**
 * @brief Initialize sample network connectivity.
 *
 * Depending on Kconfig, this either connects to Wi-Fi or starts DHCPv4 on
 * Ethernet.  Samples should call this once from main() instead of dealing
 * with the transport details themselves.
 */
void spotflow_sample_net_init(void);

#endif /* SPOTFLOW_SAMPLE_NET_H */
