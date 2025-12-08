# Spotflow Metrics Ingestion Protocol Specification

## Overview

This document defines the wire protocol for transmitting metric messages from Spotflow SDK devices to the Spotflow cloud platform. The protocol uses CBOR (Concise Binary Object Representation) encoding over MQTT transport for efficiency and reliability.

**Design Goals**:
- Minimize network bandwidth for resource-constrained devices
- Efficient encoding using integer keys and CBOR primitives
- One message per time series per aggregation interval
- Maintain consistency with existing Spotflow log/coredump protocols
- Enable lossless metric data ingestion with overflow handling

## Transport Layer

### MQTT Configuration

**Protocol**: MQTT v3.1.1 over TLS 1.2+

**Broker**: `mqtt.spotflow.io:8883`

**Topic Structure**:
```
ingestion/{provisioning_token}/{device_id}
```

**Quality of Service**: QoS 0 (at most once delivery)
- Consistent with Spotflow logs
- Acceptable for telemetry data with device-side buffering
- Reduces overhead compared to QoS 1/2

**Authentication**:
- Username: `{device_id}`
- Password: `{ingest_key}` (from Kconfig)

**Connection**: Shared with logs and coredumps
- Metrics are multiplexed with other message types on same connection
- Message type discrimination via `messageType` field

## Message Format

### CBOR Encoding

All metric messages are encoded as CBOR maps with integer keys for efficiency.

**Encoding Library**: zcbor (Zephyr provided)

**Key Optimization**: Properties use small integers (0x00-0x1F) instead of strings to reduce message size.

### Message Type Identifier

**Property**: `messageType` (key: 0x00)

**Value**: 0x05 (METRIC message type)

**Rationale**: Distinguishes metric messages from:
- 0x00: LOG
- 0x01: SESSION_METADATA
- 0x02: COREDUMP_START (assumed)
- 0x03: COREDUMP_CHUNK (assumed)
- 0x04: (reserved)
- 0x05: METRIC (this specification)

## CBOR Property Keys

### Key Registry

| Key (Hex) | Key (Dec) | Property Name | Type | Description |
|-----------|-----------|---------------|------|-------------|
| 0x00 | 0 | messageType | uint | Message discriminator (0x05 for metrics) |
| 0x05 | 5 | labels | map | Dimension key-value pairs |
| 0x06 | 6 | deviceUptimeMs | uint64 | Device monotonic clock timestamp (milliseconds) |
| 0x0D | 13 | sequenceNumber | uint64 | Per-metric monotonic sequence number |
| 0x10 | 16 | metricName | string | Metric name (case-insensitive) |
| 0x11 | 17 | aggregationInterval | uint | Aggregation interval enum |
| 0x13 | 19 | sum | int/float | Sum of values in aggregation window |
| 0x14 | 20 | sumTruncated | bool | True if sum overflowed (truncated) |
| 0x15 | 21 | count | uint64 | Number of data points in aggregation |
| 0x16 | 22 | min | int/float | Minimum value in aggregation window |
| 0x17 | 23 | max | int/float | Maximum value in aggregation window |
| 0x18 | 24 | samples | array | Individual sample values (future feature) |

**Note**: Keys 0x01-0x04, 0x07-0x0C, 0x0E-0x0F are reserved for existing/future message types.

### Aggregation Interval Enum

| Value | Constant | ISO 8601 Duration | Description |
|-------|----------|-------------------|-------------|
| 0x00 | PT0S | PT0S | No aggregation (instant/event) |
| 0x01 | PT1M | PT1M | 1 minute aggregation |
| 0x02 | PT10M | PT10M | 10 minutes aggregation |
| 0x03 | PT1H | PT1H | 1 hour aggregation |

## Message Structures

### Single Metric Message (No Dimensions)

Used for simple metrics without dimensions, or events.

**CBOR Schema**:
```
{
    0x00: uint,           // messageType = 0x05
    0x10: tstr,           // metricName
    0x11: uint,           // aggregationInterval (0-3)
    0x06: uint,           // deviceUptimeMs
    0x0D: uint,           // sequenceNumber
    0x13: int|float,      // sum
    0x14?: bool,          // sumTruncated (optional, default false)
    0x15?: uint,          // count (required if aggregationInterval != PT0S)
    0x16?: int|float,     // min (required if aggregationInterval != PT0S)
    0x17?: int|float,     // max (required if aggregationInterval != PT0S)
}
```

**Example (Diagnostic Notation)**:
```cbor-diag
{
    0: 5,                           / messageType = METRIC /
    16: "app_uptime_seconds",       / metricName /
    17: 1,                          / aggregationInterval = PT1M /
    6: 3723456,                     / deviceUptimeMs /
    13: 42,                         / sequenceNumber /
    19: 3660.5,                     / sum = 61.0083... minutes /
    21: 60,                         / count = 60 samples /
    22: 60.8,                       / min /
    23: 61.2                        / max /
}
```

**Hex Dump (Approximate)**:
```
A8                      # map(8)
   00                   # key: messageType
   04                   # value: 4 (METRIC)
   10                   # key: metricName
   73                   # string(19)
      6170705F757074696D655F7365636F6E6473  # "app_uptime_seconds"
   11                   # key: aggregationInterval
   01                   # value: 1 (PT1M)
   06                   # key: deviceUptimeMs
   1A 0038D580          # uint(3723456)
   0D                   # key: sequenceNumber
   18 2A                # uint(42)
   13                   # key: sum
   FA 41E47CCD          # float(3660.5)
   15                   # key: count
   18 3C                # uint(60)
   16                   # key: min
   FA 427399999A        # float(60.8)
   17                   # key: max
   FA 427533333         # float(61.2)
```

### Dimensional Metric Message

Used for metrics with dimensions. Each time series (unique dimension combination) sends a separate message.

**CBOR Schema**:
```
{
    0x00: uint,           // messageType = 0x05
    0x10: tstr,           // metricName
    0x05: {               // labels (dimensions)
        tstr: tstr|int|float|bool,  // key-value pairs
        ...
    },
    0x11: uint,           // aggregationInterval
    0x06: uint,           // deviceUptimeMs
    0x0D: uint,           // sequenceNumber
    0x13: int|float,      // sum
    0x14?: bool,          // sumTruncated
    0x15?: uint,          // count
    0x16?: int|float,     // min
    0x17?: int|float,     // max
}
```

**Example (CPU Temperature)**:
```cbor-diag
{
    0: 5,
    16: "cpu_temperature_celsius",
    5: {                            / labels /
        "core": "0",
        "zone": "thermal1"
    },
    17: 1,                          / aggregationInterval = PT1M /
    6: 3723456,
    13: 42,
    19: 2450.5,                     / sum /
    21: 60,                         / count /
    22: 38.2,                       / min /
    23: 43.1                        / max /
}
```

### Event Message (No Aggregation)

Events are metrics with `aggregationInterval = PT0S`. They typically represent point-in-time occurrences and are enqueued immediately to the message queue (non-blocking) for transmission.

**CBOR Schema**:
```
{
    0x00: uint,           // messageType = 0x05
    0x10: tstr,           // metricName
    0x05?: {...},         // labels (optional)
    0x11: 0,              // aggregationInterval = PT0S (no aggregation)
    0x06: uint,           // deviceUptimeMs (exact event time)
    0x0D: uint,           // sequenceNumber
    0x13: int|float,      // sum = value (typically 1.0 for occurrence)
    0x14?: bool,          // sumTruncated (optional)
    0x15: 1,              // count = 1 (always 1 for PT0S)
    // min, max are optional and typically omitted for PT0S
}
```

**Example (Boot Event)**:
```cbor-diag
{
    0: 5,
    16: "device_boot_event",
    5: {
        "reason": "power_on",
        "firmware_version": "1.2.3"
    },
    17: 0,                          / aggregationInterval = PT0S /
    6: 1234,                        / deviceUptimeMs (shortly after boot) /
    13: 0,                          / sequenceNumber (first event) /
    19: 1,                          / sum = 1 (event occurred once) /
    21: 1                           / count = 1 /
    / min, max omitted (optional for PT0S) /
}
```

## Field Semantics

### Required vs Optional Fields

**Terminology**:
- **Required**: Field MUST be present in CBOR message
- **Conditionally Required**: Field MUST be present only when specific condition is met
- **Optional**: Field MAY be present (backend handles both cases)

#### Always Required:
- `messageType` (0x00): Must be 0x05 for metrics
- `metricName` (0x10): Identifies the metric
- `aggregationInterval` (0x11): Specifies aggregation type
- `deviceUptimeMs` (0x06): Timestamp for correlation
- `sequenceNumber` (0x0D): Detects message loss
- `sum` (0x13): Primary metric value

#### Conditionally Required (for Aggregated Metrics where aggregationInterval != PT0S):
- `count` (0x15): Number of samples aggregated
- `min` (0x16): Minimum value in window
- `max` (0x17): Maximum value in window

#### Conditionally Required (for Dimensional Metrics):
- `labels` (0x05): Map of dimension key-value pairs (omitted for dimensionless metrics)

#### Optional (MAY be omitted from CBOR):
- `sumTruncated` (0x14): Only included if sum overflow detected (defaults to false)
- `count` (0x15): For PT0S messages (if included, must be 1)
- `min` (0x16): For PT0S messages (typically omitted since equals sum)
- `max` (0x17): For PT0S messages (typically omitted since equals sum)

### Field Types and Constraints

#### metricName (0x10)
- **Type**: CBOR text string (major type 3)
- **Encoding**: UTF-8
- **Constraints**:
  - Length: 1-64 characters
  - Pattern: `[a-z0-9_]+` (lowercase alphanumeric and underscore)
  - Case-insensitive handling: Backend normalizes to lowercase
- **Examples**: `"cpu_temperature_celsius"`, `"http_requests_total"`, `"memory_usage_bytes"`

#### labels (0x05)
- **Type**: CBOR map (major type 5)
- **Key Type**: CBOR text string (major type 3)
- **Value Type**: CBOR text string (major type 3) only
- **Encoding**: Both keys and values encoded as UTF-8 CBOR text strings
- **Constraints**:
  - Map size: 0-16 key-value pairs (typical: 1-4)
  - Key length: 1-32 characters
  - Value length: 1-128 characters
  - Keys are case-sensitive
  - Null or empty string values are ignored
- **Note**: Non-string values (numbers, booleans) must be converted to strings by the device SDK before CBOR encoding
- **Examples**:
  ```cbor-diag
  {
      "core": "0",
      "frequency_mhz": "1800",
      "throttled": "false",
      "voltage": "1.2"
  }
  ```

#### aggregationInterval (0x11)
- **Type**: CBOR unsigned integer (major type 0)
- **Constraints**: Value must be 0, 1, 2, or 3
- **Semantics**:
  - 0 (PT0S): No aggregation, enqueued immediately (non-blocking) for transmission
  - 1 (PT1M): Value aggregated over 1 minute
  - 2 (PT10M): Value aggregated over 10 minutes
  - 3 (PT1H): Value aggregated over 1 hour

#### deviceUptimeMs (0x06)
- **Type**: CBOR unsigned integer (major type 0), 64-bit
- **Unit**: Milliseconds since device boot (monotonic clock)
- **Constraints**: Must be ≥ 0, typically ≤ 2^53 (safe integer range)
- **Usage**:
  - Combined with SESSION_METADATA to compute absolute timestamp
  - Used for message ordering and deduplication
  - Must be monotonically increasing within a session

#### sequenceNumber (0x0D)
- **Type**: CBOR unsigned integer (major type 0), 64-bit
- **Scope**: Per-metric (not global)
- **Semantics**:
  - Starts at 0 or arbitrary value on device boot
  - Increments by 1 for each message sent for this metric
  - All time series of a dimensional metric share the sequence
  - Used to detect message loss
- **Wrapping**: If sequence reaches 2^64, wraps to 0

#### sum (0x13)
- **Type**: CBOR integer (major type 0/1) or float (major type 7)
- **Semantics**:
  - For aggregated metrics: Sum of all reported values in window
  - For non-aggregated metrics: The single reported value
  - For integer metrics: CBOR integer (int64 range: -2^63 to 2^63-1)
  - For float metrics: CBOR float (double precision: IEEE 754)
- **Overflow**: If sum exceeds representable range, set `sumTruncated = true`

#### sumTruncated (0x14)
- **Type**: CBOR boolean (major type 7)
- **Default**: false (omit field if false)
- **Semantics**: True if sum overflowed during aggregation
- **Usage**: Backend should flag metric as incomplete/unreliable

#### count (0x15)
- **Type**: CBOR unsigned integer (major type 0), 64-bit
- **Semantics**: Number of samples aggregated
- **Constraints**: Must be > 0 for aggregated metrics
- **Usage**: Calculate average: `avg = sum / count`

#### min (0x16)
- **Type**: CBOR integer or float (same type as sum)
- **Semantics**: Minimum value reported during aggregation window
- **Constraints**: `min ≤ avg ≤ max`

#### max (0x17)
- **Type**: CBOR integer or float (same type as sum)
- **Semantics**: Maximum value reported during aggregation window
- **Constraints**: `min ≤ avg ≤ max`

#### batch (0x12)
- **Type**: CBOR array (major type 4)
- **Element Type**: CBOR map (metric data point)
- **Constraints**: Array length 1-256 (typical: 1-20)
- **Status**: RESERVED for future use (not supported in v1)
- **Usage**: Would enable batching multiple time series to reduce overhead
- **Semantics**: All data points would share metric name and aggregation interval
- **Note**: v1 implementation sends one time series per message (FR-4.5)

## Message Size Considerations

### Typical Message Sizes

| Metric Type | Dimensions | Aggregated | Approximate Size (bytes) |
|-------------|------------|------------|--------------------------|
| Simple counter | 0 | Yes | 60-80 |
| Simple gauge | 0 | Yes | 60-80 |
| Dimensional counter | 2 | Yes | 100-150 |
| Dimensional gauge | 3 | Yes | 120-180 |
| Event | 2 | No | 80-120 |

### Size Optimization

**Techniques Applied**:
1. Integer keys instead of string keys: ~40% size reduction
2. CBOR compact encoding: ~20% vs JSON
3. Omit optional fields when default: ~10-20 bytes per field

**Example Comparison** (Single metric message):

| Format | Size (bytes) | Notes |
|--------|--------------|-------|
| JSON (string keys) | 260 | Baseline |
| CBOR (string keys) | 210 | ~19% reduction |
| CBOR (integer keys) | 140 | ~46% reduction vs JSON |

**Note**: Batching (0x12) is reserved for future optimization (not in v1)

### Buffer Size Recommendations

**Kconfig Setting**: `CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE`

**Recommended Values**:
- **Minimal** (simple metrics only): 256 bytes
- **Default** (typical dimensional metrics): 512 bytes
- **Extended** (complex dimensional metrics): 1024 bytes
- **Maximum**: 2048 bytes (rare, high dimension count)

**Calculation** (v1, single time series per message):
```
Buffer_Size ≈ 80 + (Metric_Name_Length) +
              (Num_Dimensions × (Key_Length + Value_Length + 20))
```

## Protocol Behavior

### Message Ordering

- Messages may arrive out of order (MQTT QoS 0)
- Backend uses `(deviceUptimeMs, sequenceNumber)` tuple for ordering
- Messages with identical tuples are deduplicated

### Message Loss Detection

- Backend tracks sequence numbers per device per metric
- Gap in sequence indicates lost message
- Backend may flag gap in UI but does not request retransmission

### Session Correlation

- Metrics are associated with SESSION_METADATA message sent at connection start
- SESSION_METADATA provides:
  - Device uptime baseline
  - Build ID (firmware version)
  - Connection timestamp
- Backend computes absolute timestamp:
  ```
  absolute_time = session_start_time + (metric.deviceUptimeMs - session.deviceUptimeMs)
  ```

### Error Handling

#### Device Side:
- Invalid CBOR encoding: Discard message, log error
- Buffer overflow during encoding: Discard message, increment error counter
- MQTT publish failure: Message lost (QoS 0), increment drop counter

#### Backend Side:
- Invalid CBOR: Reject message, log parse error
- Missing required fields: Reject message
- Invalid field types: Reject message
- Unknown messageType: Route to appropriate handler
- Unknown aggregationInterval value: Reject message

## Version Compatibility

### Current Version: 1.0

**Forward Compatibility**:
- New optional fields may be added without breaking existing parsers
- New aggregation interval values may be added (4+)
- New message type values assigned without affecting metrics (0x04)

**Backward Compatibility**:
- Adding new optional fields is backward compatible
- Changing required fields requires major version increment
- Removing fields requires major version increment

**Version Negotiation**:
- Currently no explicit version field in messages
- Backend infers version from message structure
- Future: May add version field if breaking changes needed

## Security Considerations

### Authentication
- MQTT TLS with client certificates (device identity)
- Ingest key as password (authorization)

### Data Integrity
- CBOR encoding is self-describing and validated
- TLS provides transport-level integrity

### Data Privacy
- TLS encryption in transit
- No PII should be included in metric names or dimension values
- Dimension values logged/stored by backend

### Denial of Service
- Device-side rate limiting via queue size
- Backend-side rate limiting per device (future)
- Malformed CBOR rejected quickly

## Testing and Validation

### CBOR Validation Tools

**Recommended Tools**:
- `cbor.me` - Online CBOR decoder/validator
- `cbor2` Python library - Programmatic validation
- Zephyr `zcbor` - Device-side encoding/decoding

**Validation Checklist**:
1. Message decodes without error
2. All required fields present
3. Field types match specification
4. String fields are valid UTF-8
5. Numeric values in valid ranges
6. Map keys are unique
7. Array lengths within constraints

### Test Cases

#### Test Case 1: Simple Metric
```cbor-diag
{0: 5, 16: "test_counter", 17: 1, 6: 1000, 13: 0, 19: 42, 21: 10, 22: 1, 23: 10}
```
**Expected**: Accept, parse as aggregated counter

#### Test Case 2: Dimensional Metric
```cbor-diag
{0: 5, 16: "test_temp", 5: {"sensor": "1"}, 17: 1, 6: 2000, 13: 1, 19: 25.5, 21: 5, 22: 24.0, 23: 27.0}
```
**Expected**: Accept, extract dimension

#### Test Case 3: Event (No Aggregation)
```cbor-diag
{0: 5, 16: "boot_event", 5: {"reason": "power_on"}, 17: 0, 6: 100, 13: 0, 19: 1.0}
```
**Expected**: Accept, treat as instant event

#### Test Case 4: Missing Required Field
```cbor-diag
{0: 5, 16: "bad_metric", 17: 1, 6: 1000}
```
**Expected**: Reject, missing sequenceNumber and sum

#### Test Case 5: Invalid Type
```cbor-diag
{0: 5, 16: 123, 17: 1, 6: 1000, 13: 0, 19: 42}
```
**Expected**: Reject, metricName must be string

## Implementation Notes

### Encoding Order

**Recommended Field Order** (for readability, not required):
1. messageType (0x00)
2. metricName (0x10)
3. labels (0x05) - if present
4. aggregationInterval (0x11)
5. deviceUptimeMs (0x06)
6. sequenceNumber (0x0D)
7. sum (0x13)
8. sumTruncated (0x14) - if true
10. count (0x15) - if aggregated
11. min (0x16) - if aggregated
12. max (0x17) - if aggregated

**Note**: CBOR map field order does not affect parsing, but consistent order aids debugging.

### Zcbor Code Example

```c
// Pseudo-code for encoding a simple aggregated metric
ZCBOR_STATE_E(state, 1, buffer, buffer_size, 1);

zcbor_map_start_encode(state, 8);

// messageType
zcbor_uint32_put(state, 0x00);
zcbor_uint32_put(state, 0x04);

// metricName
zcbor_uint32_put(state, 0x10);
zcbor_tstr_put_term(state, "cpu_temperature_celsius");

// aggregationInterval
zcbor_uint32_put(state, 0x11);
zcbor_uint32_put(state, 0x01);  // PT1M

// deviceUptimeMs
zcbor_uint32_put(state, 0x06);
zcbor_uint64_put(state, k_uptime_get());

// sequenceNumber
zcbor_uint32_put(state, 0x0D);
zcbor_uint64_put(state, seq_num);

// sum
zcbor_uint32_put(state, 0x13);
zcbor_float64_put(state, sum_value);

// count
zcbor_uint32_put(state, 0x15);
zcbor_uint64_put(state, count_value);

// min
zcbor_uint32_put(state, 0x16);
zcbor_float64_put(state, min_value);

// max
zcbor_uint32_put(state, 0x17);
zcbor_float64_put(state, max_value);

zcbor_map_end_encode(state, 8);

size_t encoded_len = state->payload - buffer;
```

## Appendix A: Complete Message Examples

### Example A1: Simple Counter (Hex)

**Metric**: HTTP request counter, 1-minute aggregation

**CBOR Diagnostic**:
```cbor-diag
{
    0: 5,
    16: "http_requests_total",
    17: 1,
    6: 60000,
    13: 5,
    19: 150,
    21: 150,
    22: 1,
    23: 1
}
```

**CBOR Hex**:
```
A8 00 05 10 74 68 74 74 70 5F 72 65 71 75 65 73
74 73 5F 74 6F 74 61 6C 11 01 06 19 EA 60 0D 05
13 18 96 15 18 96 16 01 17 01
```

### Example A2: Multi-Dimensional Temperature (Hex)

**Metric**: CPU temperature per core, 10-minute aggregation

**CBOR Diagnostic**:
```cbor-diag
{
    0: 5,
    16: "cpu_temperature_celsius",
    5: {"core": "0", "zone": "thermal1"},
    17: 2,
    6: 600000,
    13: 12,
    19: 22800.0,
    21: 600,
    22: 35.0,
    23: 42.5
}
```

**CBOR Hex** (abbreviated):
```
A8 00 05 10 77 63 70 75 5F 74 65 6D ... [continues]
```

## Appendix B: Backend Processing

### Ingestion Pipeline

```
Device → MQTT Broker → Ingestion Service → Validation → Time Series DB → Query API
```

**Steps**:
1. **Receive**: MQTT message from device
2. **Decode**: CBOR to internal representation
3. **Validate**: Schema compliance, required fields, types
4. **Enrich**: Add absolute timestamp, device metadata
5. **Store**: Write to time series database
6. **Index**: Update metric/label indexes
7. **Alert**: Trigger alerts if configured

### Database Schema (Conceptual)

```sql
-- Metrics table
CREATE TABLE metrics (
    device_id TEXT,
    metric_name TEXT,
    timestamp TIMESTAMP,
    uptime_ms BIGINT,
    sequence_number BIGINT,
    aggregation_interval INT,
    sum DOUBLE,
    count BIGINT,
    min DOUBLE,
    max DOUBLE,
    sum_truncated BOOLEAN,
    labels JSONB,  -- Key-value pairs
    PRIMARY KEY (device_id, metric_name, timestamp, labels)
);

-- Indexes
CREATE INDEX idx_metrics_device_time ON metrics(device_id, timestamp);
CREATE INDEX idx_metrics_name ON metrics(metric_name);
CREATE INDEX idx_metrics_labels ON metrics USING GIN(labels);
```

## Appendix C: References

1. **CBOR RFC 8949**: https://datatracker.ietf.org/doc/html/rfc8949
2. **Zephyr zcbor Documentation**: https://docs.zephyrproject.org/latest/services/serialization/zcbor.html
3. **ISO 8601 Duration**: https://en.wikipedia.org/wiki/ISO_8601#Durations
4. **Spotflow Documentation**: https://docs.spotflow.io
5. **MQTT v3.1.1 Specification**: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html

## Document Version

**Version**: 1.0.0

**Last Updated**: 2025-12-05

**Status**: Draft for Implementation

**Authors**: Spotflow SDK Architecture Team
