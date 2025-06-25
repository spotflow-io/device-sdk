#include "spotflow_cbor_output_context.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cbor_output_context_spotflow, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

int spotflow_cbor_output_context_init(struct spotflow_cbor_output_context** _context)
{
	__ASSERT_NO_MSG(_context != NULL);

	struct spotflow_cbor_output_context* context =
	    k_malloc(sizeof(struct spotflow_cbor_output_context));
	if (context == NULL) {
		return -ENOMEM;
	}

	*_context = context;
	return 0;
}

/* not used because output context is used for the whole lifetime of application */
void spotflow_cbor_output_context_free(struct spotflow_cbor_output_context* context)
{
	if (context != NULL) {
		k_free(context);
	}
}
