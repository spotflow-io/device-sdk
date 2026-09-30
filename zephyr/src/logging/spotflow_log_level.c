#include "spotflow_log_level.h"

#include <zephyr/logging/log.h>

uint32_t spotflow_log_level_to_severity(uint8_t lvl)
{
	switch (lvl) {
	case LOG_LEVEL_ERR:
		return 60;
	case LOG_LEVEL_WRN:
		return 50;
	case LOG_LEVEL_INF:
		return 40;
	case LOG_LEVEL_DBG:
		return 30;
	default:
		return 0; /* unknown level */
	}
}

uint8_t spotflow_log_level_from_severity(uint32_t severity)
{
	switch (severity) {
	case 70:
	case 60:
		return LOG_LEVEL_ERR;
	case 50:
		return LOG_LEVEL_WRN;
	case 40:
		return LOG_LEVEL_INF;
	case 30:
		return LOG_LEVEL_DBG;
	default:
		return LOG_LEVEL_DBG;
	}
}
