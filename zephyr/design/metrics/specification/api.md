// API

// maximal timeseries count (combination of values of dimensions)
t_metric * dimension_metric = register_metric_float(name, maxConcurrentTimeSeriesCount, maxDimensionsCount, collectSamples = false // Later)
register_metric_int()

t_metric * dimensionless_metric = register_metric_simple(name, collectSamples = false)

struct dimension_keyvalue {
char * key;

	char * value;
	// OR
	enum dimension_value_type value_type;
	union dimension_value value;
}

dimension_keyvalue * dimensions =

int error = report_metric(..., dimensions, x) // Send Log (warnings) to cloud ?
int error = report_event(...)
