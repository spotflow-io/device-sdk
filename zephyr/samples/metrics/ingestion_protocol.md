## Metric message **(TODO)**

Message representing one aggregated data point. See [Metrics](https://www.notion.so/Metrics-279400d453c58035bfe0da10a3a8c3b2?pvs=21) for details.

| **Property** | **Description**                                                                                                                        |
| --- |----------------------------------------------------------------------------------------------------------------------------------------|
| `messageType` | `METRIC`                                                                                                                               |
| `metricName`  | Required. String representing name of the metric. Metric name is handled in case-insensitive manner and is normalized into lower-case. |
| `labels` | Optional, user-defined key-value pairs with string, non-null, non-empty keys and values of strings, numbers or bools.Null and empty string values are ignored.Message-level and session-level labels are merged.Message-level labels always have precedence over session-level labels. |
| `aggregationInterval` | Optional. Enum value indicating how long the interval covered by current metric message is. Values:• `PT0S` = `0x00 = 0`• `PT1M` = `0x01 = 1`• `PT10M` = `0x02 = 2`• `PT1H` = `0x03 = 3` If not specified, no aggregation (`PT0S`) is assumed and the metric message represents single specific data point.

*The textual representation of enum values is [ISO 8601 duration](https://en.wikipedia.org/wiki/ISO_8601).* |
| `batch` | Optional. Array of objects with all following properties. |
| `deviceUptimeMs` | Required 64bit integer. Current timestamp from device’s monotonic clock.

The `deviceUptimeMs` property is used together with `deviceUptimeMs` property from `SESSION_METADATA` to calculate precise metric timestamp. If currently available `SESSION_METADATA` message did not specify `deviceUptimeMs`, the current cloud time is used as the metric timestamp. |
| `sequenceNumber`  | Optional, 64bit integer. Continuous monotonic sequence number of the metric message in a sequence.

Can be used to detect missing messages.

Each metric represent has its own sequence, different from other metrics. The message for a single metric with different dimensions share the sequence. |
| `sum`  | Required, integer or float up to 64 bits.

Sum of values of all data points in the aggregation. |
| `sumTruncated` | Optional, boolean. If `true`, the provided value of `sum` is truncated and actual value is higher by unknown amount. |
| `count` | Optional, integer  up to 64 bits.

Number of data points in the aggregation.

Must be specified when `aggregationInterval` is not null. |
| `min` | Optional, integer or float up to 64bits.

Maximum value from all data points in the aggregation.

Must be specified when `aggregationInterval` is not null. |
| `max` | Optional, integer or float up to 64bits.

Minimum value from all data points in the aggregation.

Must be specified when `aggregationInterval` is not null. |
| `samples`  | Optional, array of integers or floats up to 64 bits. |

**JSON example (single)**

```json
{
	"messageType": "METRIC",
	"metricName": "XXX",
	"labels": {
		"key1": "value"
	},
	"aggregationInterval": "PT1M",
	"deviceUptimeMs": 42,
	"sequenceNumber": 0,
	"sum": 8.2,
	"count": 2,
	"minValue": 1,
	"maxValue": 7.2,
	"samples": [1, 7.2]
}
```

