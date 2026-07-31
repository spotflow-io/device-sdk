#include "ota/downloader/spotflow_ota_downloader_transport.h"

#include <spotflow/downloader.h>

#include "ota/downloader/spotflow_ota_downloader.h"
#include "ota/downloader/spotflow_ota_downloader_transport_range.h"
#include "ota/downloader/spotflow_ota_url.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define OTA_TLS_SEC_TAG 1
#define OTA_RANGE_HEADER_MAX_LEN 48

struct spotflow_ota_downloader_http_ctx {
	struct spotflow_downloader* downloader;
	spotflow_download_block_callback callback;
	void* callback_ctx;
	size_t range_start;
	size_t offset;
	uint64_t* artifact_size;
	int callback_err;
	bool paused;
	bool transient_failure;
	bool response_validated;
};

static int connect_socket(const struct spotflow_ota_url* url);
static int http_response_cb(struct http_response* rsp, enum http_final_call final_data,
			    void* user_data);

int spotflow_ota_downloader_transport_download(
	struct spotflow_ota_downloader_transport_request* request)
{
	if (request == NULL || request->url == NULL || request->authorization_header == NULL ||
	    request->downloader == NULL || request->callback == NULL ||
	    request->bytes_downloaded == NULL || request->artifact_size == NULL ||
	    request->paused == NULL || request->transient_failure == NULL) {
		return -EINVAL;
	}

	*request->paused = false;
	*request->transient_failure = false;
	*request->bytes_downloaded = 0;

	k_timepoint_t request_deadline =
		sys_timepoint_calc(K_MSEC(CONFIG_SPOTFLOW_OTA_HTTP_TIMEOUT_MS));
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
		.artifact_size = request->artifact_size,
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

	if (sys_timepoint_expired(request_deadline)) {
		zsock_close(sock);
		spotflow_ota_downloader_transport_note_error(request, 0, -ETIMEDOUT);
		return -ETIMEDOUT;
	}

	int32_t http_timeout_ms =
		k_ticks_to_ms_ceil32(sys_timepoint_timeout(request_deadline).ticks);
	int rc = http_client_req(sock, &req, http_timeout_ms, &http_ctx);

	zsock_close(sock);

	if (http_ctx.callback_err != 0) {
		*request->bytes_downloaded = http_ctx.offset - request->range_start;
		*request->paused = http_ctx.paused;
		*request->transient_failure = http_ctx.transient_failure;
		if (!http_ctx.paused) {
			spotflow_ota_downloader_transport_note_error(
				request, *request->bytes_downloaded, http_ctx.callback_err);
		}
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

static int connect_socket(const struct spotflow_ota_url* url)
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

	int rc = spotflow_ota_downloader_check_state(ctx->downloader);

	if (rc != 0) {
		ctx->paused = rc == -EAGAIN;
		ctx->callback_err = rc;
		return rc;
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

	if (ctx->range_start > 0 && !ctx->response_validated) {
		rc = spotflow_ota_downloader_transport_validate_range_response(
			rsp, ctx->range_start, ctx->artifact_size);

		if (rc != 0) {
			LOG_ERR("Artifact download returned invalid Content-Range");
			ctx->callback_err = rc;
			return rc;
		}

		ctx->response_validated = true;
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

	} else if (final_data == HTTP_DATA_FINAL) {
		struct spotflow_artifact_block block = {
			.offset = ctx->offset,
			.data = NULL,
			.data_len = 0,
			.is_last = true,
		};

		ctx->callback(&block, ctx->downloader, ctx->callback_ctx);
	}

	if (final_data == HTTP_DATA_FINAL) {
		return 0;
	}

	rc = spotflow_ota_downloader_check_state(ctx->downloader);
	if (rc != 0) {
		ctx->paused = rc == -EAGAIN;
		ctx->callback_err = rc;
		return rc;
	}

	return 0;
}
