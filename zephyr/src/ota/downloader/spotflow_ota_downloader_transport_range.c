#include <errno.h>
#include <limits.h>

#include "ota/downloader/spotflow_ota_downloader_transport_range.h"

int spotflow_ota_downloader_transport_validate_range_response(const struct http_response* response,
							      size_t range_start,
							      uint64_t* artifact_size)
{
	if (response == NULL || artifact_size == NULL ||
	    response->content_range.start != range_start || response->content_range.total == 0 ||
	    response->content_range.end != response->content_range.total - 1 ||
	    response->content_range.end < response->content_range.start ||
	    response->content_range.total > SIZE_MAX) {
		return -EPROTO;
	}

	if (*artifact_size != 0 && *artifact_size != response->content_range.total) {
		return -EPROTO;
	}

	*artifact_size = response->content_range.total;

	return 0;
}
