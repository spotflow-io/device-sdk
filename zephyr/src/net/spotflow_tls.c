#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>

#include "net/spotflow_certs.h"

LOG_MODULE_REGISTER(SPOTFLOW_TLS, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

#define APP_CA_ISGROOTX1_CERT_TAG 1

static int tls_certificate_add(const unsigned char *cert, size_t cert_len, int tag);

static sec_tag_t m_sec_tags[] = {APP_CA_ISGROOTX1_CERT_TAG};

void spotflow_tls_configure(const char *hostname, struct mqtt_sec_config *tls_config) {
	tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
	tls_config->hostname = hostname;
}

int spotflow_tls_init(void) {
	LOG_DBG("TLS init");
	int err = tls_certificate_add(spotflow_isrgrootx1_der,
		sizeof(spotflow_isrgrootx1_der),
		APP_CA_ISGROOTX1_CERT_TAG);
	if (err < 0) {
		LOG_ERR("Failed to register public certificate spotflow_isrgrootx1_der: %d", err);
		return err;
	}
	return err;
}


static int tls_certificate_add(const unsigned char *cert, size_t cert_len, int tag) {
	int err = tls_credential_add(tag, TLS_CREDENTIAL_CA_CERTIFICATE, cert, cert_len);
	if (err == -EEXIST) {
		/* already there – that's fine */
		LOG_DBG("Certificate already in store, continuing.");
		return 0;
	}
	if (err < 0) {
		LOG_ERR("Failed to register public certificate: %d", err);
		return err;
	}
	return 0;
}
