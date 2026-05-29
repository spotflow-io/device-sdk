#ifndef SPOTFLOW_OTA_URL_H
#define SPOTFLOW_OTA_URL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ota_url {
	bool tls;
	char host[128];
	char path[128];
	uint16_t port;
};

int spotflow_ota_parse_url(const char* url, struct ota_url* out);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_URL_H */
