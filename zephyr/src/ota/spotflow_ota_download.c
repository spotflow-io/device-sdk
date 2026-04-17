#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/printk.h>

#include "ota/spotflow_ota_download.h"
#include "ota/spotflow_ota_url.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

/* TLS sec_tag used for the Spotflow MQTT connection -- reuse the same root CA. */
#define OTA_TLS_SEC_TAG 1

/* Timeout for the full HTTP download in milliseconds. */
#define HTTP_DOWNLOAD_TIMEOUT_MS 120000

struct ota_flash_ctx {
	struct flash_img_context fctx;
	int write_err;
};

static int connect_socket(const struct ota_url* url);

static int http_response_cb(struct http_response* rsp, enum http_final_call final_data,
			    void* user_data);

int spotflow_ota_download_and_flash(const char* image_url)
{
	struct ota_url url;
	int rc = spotflow_ota_parse_url(image_url, &url);

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

static int connect_socket(const struct ota_url* url)
{
	struct zsock_addrinfo hints = {
		.ai_socktype = SOCK_STREAM,
		.ai_family = AF_INET,
	};
	struct zsock_addrinfo* res = NULL;
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

		rc = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags, sizeof(sec_tags));
		if (rc < 0) {
			LOG_ERR("Failed to set TLS sec tag: %d", errno);
			zsock_close(sock);
			zsock_freeaddrinfo(res);
			return -errno;
		}

		rc =
		    zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, url->host, strlen(url->host) + 1);
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

static int http_response_cb(struct http_response* rsp, enum http_final_call final_data,
			    void* user_data)
{
	struct ota_flash_ctx* ctx = user_data;

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
		int rc = flash_img_buffered_write(&ctx->fctx, NULL, 0, true);

		if (rc < 0) {
			LOG_ERR("flash_img_buffered_write flush failed: %d", rc);
			ctx->write_err = rc;
			return rc;
		}
	}

	return 0;
}
