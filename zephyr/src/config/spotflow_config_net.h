#ifndef SPOTFLOW_CONFIG_NET_H
#define SPOTFLOW_CONFIG_NET_H

#include "config/spotflow_config_cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_config_prepare_pending_message(struct spotflow_config_reported_msg* reported_msg);
int spotflow_config_send_pending_message();

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_NET_H */
