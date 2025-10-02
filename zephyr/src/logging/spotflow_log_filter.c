#include "spotflow_log_filter.h"

#include <stdint.h>

bool spotflow_log_filter_allow_msg(struct log_msg* log_msg)
{
        uint8_t level = log_msg_get_level(log_msg);
        return level <= CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL;
}
