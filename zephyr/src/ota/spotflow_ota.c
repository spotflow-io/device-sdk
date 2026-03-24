#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser_url.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_cbor.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

/* TLS sec_tag used for the Spotflow MQTT connection -- reuse the same root CA. */
#define OTA_TLS_SEC_TAG 1

/* Timeout for the full HTTP download in milliseconds. */
#define HTTP_DOWNLOAD_TIMEOUT_MS 120000

struct ota_url {
	bool tls;
	char host[64];
	char path[64];
	uint16_t port;
};

struct ota_flash_ctx {
	struct flash_img_context fctx;
	int write_err;
};

static char s_image_url[SPOTFLOW_OTA_IMAGE_URL_MAX_LENGTH + 1];
static K_SEM_DEFINE(s_download_sem, 0, 1);

static void ota_download_thread_entry(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(spotflow_ota_thread, CONFIG_SPOTFLOW_OTA_DOWNLOAD_THREAD_STACK_SIZE,
		ota_download_thread_entry, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		0);

/* --------------------------------------------------------------------------
 * URL parsing
 * -------------------------------------------------------------------------- */

/**
 * @brief Parse an HTTP/HTTPS URL into its components using http_parser_parse_url.
 *
 * Supports http and https schemes with optional port and path+query.
 * Path defaults to "/" when absent. Query string is appended to path so that
 * pre-signed URLs (e.g. with ?token=...) are forwarded correctly.
 *
 * @return 0 on success, -EINVAL on malformed or unsupported input
 */
static int parse_url(const char *url, struct ota_url *out)
{
	struct http_parser_url parsed;

	http_parser_url_init(&parsed);

	if (http_parser_parse_url(url, strlen(url), 0, &parsed) != 0) {
		LOG_ERR("Failed to parse URL: %s", url);
		return -EINVAL;
	}

	/* Scheme */
	if (!(parsed.field_set & BIT(UF_SCHEMA))) {
		LOG_ERR("URL missing scheme: %s", url);
		return -EINVAL;
	}

	uint16_t schema_off = parsed.field_data[UF_SCHEMA].off;
	uint16_t schema_len = parsed.field_data[UF_SCHEMA].len;

	if (schema_len == 5 && strncmp(url + schema_off, "https", 5) == 0) {
		out->tls = true;
		out->port = 443;
	} else if (schema_len == 4 && strncmp(url + schema_off, "http", 4) == 0) {
		out->tls = false;
		out->port = 80;
	} else {
		LOG_ERR("Unsupported URL scheme in: %s", url);
		return -EINVAL;
	}

	/* Host */
	if (!(parsed.field_set & BIT(UF_HOST))) {
		LOG_ERR("URL missing host: %s", url);
		return -EINVAL;
	}

	uint16_t host_off = parsed.field_data[UF_HOST].off;
	uint16_t host_len = parsed.field_data[UF_HOST].len;

	if (host_len >= sizeof(out->host)) {
		LOG_ERR("Host field too long in URL: %s", url);
		return -EINVAL;
	}

	memcpy(out->host, url + host_off, host_len);
	out->host[host_len] = '\0';

	/* Port (parsed.port is already a uint16_t; 0 means field absent) */
	if (parsed.field_set & BIT(UF_PORT)) {
		out->port = parsed.port;
	}

	/* Path + optional query string (query is contiguous after '?' in the URL) */
	if (parsed.field_set & BIT(UF_PATH)) {
		uint16_t path_off = parsed.field_data[UF_PATH].off;
		uint16_t copy_len = parsed.field_data[UF_PATH].len;

		if (parsed.field_set & BIT(UF_QUERY)) {
			/* Extend to include the '?' separator and the query string */
			copy_len = (parsed.field_data[UF_QUERY].off +
				    parsed.field_data[UF_QUERY].len) -
				   path_off;
		}

		if (copy_len >= sizeof(out->path)) {
			LOG_ERR("Path field too long in URL: %s", url);
			return -EINVAL;
		}

		memcpy(out->path, url + path_off, copy_len);
		out->path[copy_len] = '\0';
	} else {
		out->path[0] = '/';
		out->path[1] = '\0';
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * HTTP response callback -- streams body into flash
 * -------------------------------------------------------------------------- */

static int http_response_cb(struct http_response *rsp, enum http_final_call final_data,
			     void *user_data)
{
	struct ota_flash_ctx *ctx = user_data;

	if (ctx->write_err != 0) {
		return -ECANCELED;
	}

	if (rsp->http_status_code != 0 && rsp->http_status_code != 200) {
		LOG_ERR("HTTP server returned status %u (%s)", rsp->http_status_code,
			rsp->http_status);
		ctx->write_err = -EPROTO;
		return -EPROTO;
	}

	if (rsp->body_frag_len > 0) {
		bool flush = (final_data == HTTP_DATA_FINAL);
		int rc = flash_img_buffered_write(&ctx->fctx, rsp->body_frag_start,
						  rsp->body_frag_len, flush);

		if (rc < 0) {
			LOG_ERR("flash_img_buffered_write failed: %d", rc);
			ctx->write_err = rc;
			return rc;
		}
	} else if (final_data == HTTP_DATA_FINAL) {
		/* Last call with no new body data -- flush any remainder. */
		int rc = flash_img_buffered_write(&ctx->fctx, NULL, 0, true);

		if (rc < 0) {
			LOG_ERR("flash_img_buffered_write flush failed: %d", rc);
			ctx->write_err = rc;
			return rc;
		}
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * Download and flash a firmware image
 * -------------------------------------------------------------------------- */

static int connect_socket(const struct ota_url *url)
{
	struct zsock_addrinfo hints = {
		.ai_socktype = SOCK_STREAM,
		.ai_family = AF_INET,
	};
	struct zsock_addrinfo *res = NULL;
	char port_str[6];

	snprintk(port_str, sizeof(port_str), "%u", url->port);

	int rc = zsock_getaddrinfo(url->host, port_str, &hints, &res);

	if (rc != 0) {
		LOG_ERR("DNS lookup failed for %s: %d", url->host, rc);
		return -EHOSTUNREACH;
	}

	int sock;

	if (url->tls) {
		sock = zsock_socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);
	} else {
		sock = zsock_socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
	}

	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		zsock_freeaddrinfo(res);
		return -errno;
	}

	if (url->tls) {
		sec_tag_t sec_tags[] = { OTA_TLS_SEC_TAG };

		rc = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags,
				      sizeof(sec_tags));
		if (rc < 0) {
			LOG_ERR("Failed to set TLS sec tag: %d", errno);
			zsock_close(sock);
			zsock_freeaddrinfo(res);
			return -errno;
		}

		rc = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, url->host,
				      strlen(url->host) + 1);
		if (rc < 0) {
			LOG_ERR("Failed to set TLS hostname: %d", errno);
			zsock_close(sock);
			zsock_freeaddrinfo(res);
			return -errno;
		}
	}

	rc = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);

	if (rc < 0) {
		LOG_ERR("Failed to connect to %s:%u: %d", url->host, url->port, errno);
		zsock_close(sock);
		return -errno;
	}

	return sock;
}

static int download_and_flash(const char *image_url)
{
	struct ota_url url;
	int rc = parse_url(image_url, &url);

	if (rc < 0) {
		return rc;
	}

	LOG_INF("Connecting to %s:%u (TLS: %d)", url.host, url.port, url.tls);

	int sock = connect_socket(&url);

	if (sock < 0) {
		return sock;
	}

	struct ota_flash_ctx flash_ctx = { .write_err = 0 };

	rc = flash_img_init(&flash_ctx.fctx);
	if (rc < 0) {
		LOG_ERR("flash_img_init failed: %d", rc);
		zsock_close(sock);
		return rc;
	}

	static uint8_t recv_buf[CONFIG_IMG_BLOCK_BUF_SIZE];

	struct http_request req = {
		.method = HTTP_GET,
		.url = url.path,
		.host = url.host,
		.protocol = "HTTP/1.1",
		.response = http_response_cb,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	LOG_INF("Starting HTTP GET %s%s", url.host, url.path);

	rc = http_client_req(sock, &req, HTTP_DOWNLOAD_TIMEOUT_MS, &flash_ctx);
	zsock_close(sock);

	if (rc < 0) {
		LOG_ERR("http_client_req failed: %d", rc);
		return rc;
	}

	if (flash_ctx.write_err != 0) {
		LOG_ERR("Flash write error during download: %d", flash_ctx.write_err);
		return flash_ctx.write_err;
	}

	LOG_INF("Downloaded %zu bytes to flash slot %u", flash_img_bytes_written(&flash_ctx.fctx),
		flash_img_get_upload_slot());

	return 0;
}

/* --------------------------------------------------------------------------
 * OTA download thread
 * -------------------------------------------------------------------------- */

static void ota_download_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&s_download_sem, K_FOREVER);

		LOG_INF("OTA download triggered for: %s", s_image_url);

		int rc = download_and_flash(s_image_url);

		if (rc < 0) {
			LOG_ERR("OTA firmware download failed: %d -- will retry on next update "
				"message",
				rc);
			continue;
		}

		rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
		if (rc < 0) {
			LOG_ERR("boot_request_upgrade failed: %d", rc);
			continue;
		}

		LOG_INF("OTA image written and marked for test boot -- rebooting");
		sys_reboot(SYS_REBOOT_COLD);
	}
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

static void handle_update_firmware_msg(uint8_t *payload, size_t len);

int spotflow_ota_init_session(void)
{
	int rc = spotflow_mqtt_request_ota_subscription(handle_update_firmware_msg);
	if (rc < 0) {
		LOG_ERR("Failed to request subscription to OTA topic: %d", rc);
		return rc;
	}

	return 0;
}

static void handle_update_firmware_msg(uint8_t *payload, size_t len)
{
	struct spotflow_ota_update_firmware_msg msg;
	int rc = spotflow_ota_cbor_decode_update_firmware(payload, len, &msg);
	if (rc < 0) {
		LOG_ERR("Failed to decode received OTA update firmware message: %d", rc);
		return;
	}

	/* TODO: Replace just by the version after the message includes it (the URL is a secret) */
	LOG_INF("OTA firmware update requested: %s", msg.image_url);

	strncpy(s_image_url, msg.image_url, sizeof(s_image_url) - 1);
	s_image_url[sizeof(s_image_url) - 1] = '\0';

	k_sem_give(&s_download_sem);
}
