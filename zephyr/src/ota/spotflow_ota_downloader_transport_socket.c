#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <spotflow/downloader.h>

#include "ota/spotflow_ota_downloader_transport.h"
#include "ota/spotflow_ota_url.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define OTA_TLS_SEC_TAG 1
#define OTA_RANGE_HEADER_MAX_LEN 48

struct spotflow_ota_downloader_http_ctx {
	struct spotflow_downloader* downloader;
	spotflow_download_block_callback callback;
	void* callback_ctx;
	size_t range_start;
	size_t offset;
	int callback_err;
	bool transient_failure;
};

static int connect_socket(const struct ota_url* url);
static int http_response_cb(struct http_response* rsp, enum http_final_call final_data,
			    void* user_data);
static bool downloader_is_canceled(struct spotflow_downloader* downloader);

int spotflow_ota_downloader_transport_download(
    struct spotflow_ota_downloader_transport_request* request)
{
	if (request == NULL || request->url == NULL || request->authorization_header == NULL ||
	    request->downloader == NULL || request->callback == NULL ||
	    request->bytes_downloaded == NULL || request->transient_failure == NULL) {
		return -EINVAL;
	}

	*request->transient_failure = false;
	*request->bytes_downloaded = 0;

	int sock = connect_socket(request->url);

	if (sock < 0) {
		spotflow_ota_downloader_transport_note_error(request, 0, sock);
		return sock;
	}

	struct spotflow_ota_downloader_http_ctx http_ctx = {
		.downloader = request->downloader,
		.callback = request->callback,
		.callback_ctx = request->callback_ctx,
		.range_start = request->range_start,
		.offset = request->range_start,
		.callback_err = 0,
		.transient_failure = false,
	};

	static uint8_t recv_buf[CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE];
	char range_header[OTA_RANGE_HEADER_MAX_LEN];
	const char* optional_headers[3];
	size_t optional_header_count = 0;

	optional_headers[optional_header_count++] = request->authorization_header;

	if (request->range_start > 0) {
		int written = snprintk(range_header, sizeof(range_header), "Range: bytes=%zu-\r\n",
				       request->range_start);

		if (written < 0 || (size_t)written >= sizeof(range_header)) {
			zsock_close(sock);
			spotflow_ota_downloader_transport_note_error(request, 0, -ENOMEM);
			return -ENOMEM;
		}

		optional_headers[optional_header_count++] = range_header;
	}

	optional_headers[optional_header_count] = NULL;

	struct http_request req = {
		.method = HTTP_GET,
		.url = request->url->path,
		.host = request->url->host,
		.protocol = "HTTP/1.1",
		.response = http_response_cb,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
		.optional_headers = optional_headers,
	};

	int rc = http_client_req(sock, &req, CONFIG_SPOTFLOW_OTA_HTTP_TIMEOUT_MS, &http_ctx);

	zsock_close(sock);

	if (http_ctx.callback_err != 0) {
		*request->bytes_downloaded = http_ctx.offset - request->range_start;
		*request->transient_failure = http_ctx.transient_failure;
		spotflow_ota_downloader_transport_note_error(request, *request->bytes_downloaded,
							     http_ctx.callback_err);
		return http_ctx.callback_err;
	}

	if (rc < 0) {
		*request->bytes_downloaded = http_ctx.offset - request->range_start;
		spotflow_ota_downloader_transport_note_error(request, *request->bytes_downloaded,
							     rc);
		return rc;
	}

	*request->bytes_downloaded = http_ctx.offset - request->range_start;
	return 0;
}

static bool downloader_is_canceled(struct spotflow_downloader* downloader)
{
	bool canceled;

	k_mutex_lock(&downloader->mutex, K_FOREVER);
	canceled = downloader->cancel_requested;
	k_mutex_unlock(&downloader->mutex);

	return canceled;
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
		LOG_ERR("DNS lookup failed for artifact host: %d", rc);
		return -EHOSTUNREACH;
	}

	int sock;

	if (url->tls) {
		sock = zsock_socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);
	} else {
		sock = zsock_socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
	}

	if (sock < 0) {
		LOG_ERR("Failed to create download socket: %d", errno);
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
		LOG_ERR("Failed to connect to artifact download endpoint on port %u: %d", url->port,
			errno);
		zsock_close(sock);
		return -errno;
	}

	return sock;
}

static int http_response_cb(struct http_response* rsp, enum http_final_call final_data,
			    void* user_data)
{
	struct spotflow_ota_downloader_http_ctx* ctx = user_data;

	if (ctx->callback_err != 0) {
		return ctx->callback_err;
	}

	if (downloader_is_canceled(ctx->downloader)) {
		ctx->callback_err = -ECANCELED;
		return -ECANCELED;
	}

	if (rsp->http_status_code != 0) {
		const bool ok_status = ctx->range_start == 0 ? rsp->http_status_code == 200
							     : rsp->http_status_code == 206;

		if (!ok_status) {
			LOG_ERR("Artifact download returned HTTP status %u", rsp->http_status_code);
			ctx->callback_err = -EPROTO;
			ctx->transient_failure = rsp->http_status_code >= 500;
			return ctx->callback_err;
		}
	}

	if (rsp->body_frag_len > 0) {
		struct spotflow_artifact_block block = {
			.offset = ctx->offset,
			.data = rsp->body_frag_start,
			.data_len = rsp->body_frag_len,
			.is_last = final_data == HTTP_DATA_FINAL,
		};

		ctx->callback(&block, ctx->downloader, ctx->callback_ctx);
		ctx->offset += rsp->body_frag_len;

		if (downloader_is_canceled(ctx->downloader)) {
			ctx->callback_err = -ECANCELED;
			return -ECANCELED;
		}
	} else if (final_data == HTTP_DATA_FINAL) {
		struct spotflow_artifact_block block = {
			.offset = ctx->offset,
			.data = NULL,
			.data_len = 0,
			.is_last = true,
		};

		ctx->callback(&block, ctx->downloader, ctx->callback_ctx);
	}

	return 0;
}
