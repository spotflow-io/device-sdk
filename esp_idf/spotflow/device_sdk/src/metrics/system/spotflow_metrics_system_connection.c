#include "metrics/system/spotflow_metrics_system_connection.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "spotflow.h"

static struct spotflow_metric_int* g_connection_state_metric;

int spotflow_metrics_system_connection_init(void)
{
	int rc =
	    spotflow_register_metric_int(SPOTFLOW_METRIC_NAME_CONNECTION,
					 SPOTFLOW_AGG_INTERVAL_NONE, &g_connection_state_metric);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to register connection state metric: %d", rc);
		return rc;
	}

	SPOTFLOW_LOG("Registered connection state metric");
	return 1;
}

void spotflow_metrics_system_connection_report(bool connected)
{
	if (!g_connection_state_metric) {
		/*This should never happen because spotflow_metrics_system_report_connection_state
		 *checks if system metrics were already initialized*/
		SPOTFLOW_LOG("Connection state metric not registered");
		return;
	}

	int rc = spotflow_report_metric_int(g_connection_state_metric, connected ? 1 : 0);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to report connection state: %d", rc);
		return;
	}

	SPOTFLOW_LOG("MQTT connection state: %s", connected ? "connected" : "disconnected");
}
