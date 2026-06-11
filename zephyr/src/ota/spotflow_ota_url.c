#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/parser_url.h>
#include <zephyr/sys/util.h>

#include "ota/spotflow_ota_url.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

int spotflow_ota_parse_url(const char* url, struct ota_url* out)
{
	struct http_parser_url parsed;

	http_parser_url_init(&parsed);

	if (http_parser_parse_url(url, strlen(url), 0, &parsed) != 0) {
		LOG_ERR("Failed to parse artifact URL");
		return -EINVAL;
	}

	if (!(parsed.field_set & BIT(UF_SCHEMA))) {
		LOG_ERR("Artifact URL missing scheme");
		return -EINVAL;
	}

	uint16_t schema_off = parsed.field_data[UF_SCHEMA].off;
	uint16_t schema_len = parsed.field_data[UF_SCHEMA].len;

	if (schema_len == 5 && strncmp(url + schema_off, "https", 5) == 0) {
		out->tls = true;
		out->port = 443;
	} else {
		LOG_ERR("Unsupported artifact URL scheme");
		return -EINVAL;
	}

	if (!(parsed.field_set & BIT(UF_HOST))) {
		LOG_ERR("Artifact URL missing host");
		return -EINVAL;
	}

	uint16_t host_off = parsed.field_data[UF_HOST].off;
	uint16_t host_len = parsed.field_data[UF_HOST].len;

	if (host_len >= sizeof(out->host)) {
		LOG_ERR("Artifact URL host field too long");
		return -EINVAL;
	}

	memcpy(out->host, url + host_off, host_len);
	out->host[host_len] = '\0';

	if (parsed.field_set & BIT(UF_PORT)) {
		out->port = parsed.port;
	}

	if (parsed.field_set & BIT(UF_PATH)) {
		uint16_t path_off = parsed.field_data[UF_PATH].off;
		uint16_t copy_len = parsed.field_data[UF_PATH].len;

		if (parsed.field_set & BIT(UF_QUERY)) {
			copy_len =
			    (parsed.field_data[UF_QUERY].off + parsed.field_data[UF_QUERY].len) -
			    path_off;
		}

		if (copy_len >= sizeof(out->path)) {
			LOG_ERR("Artifact URL path field too long");
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
