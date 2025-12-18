# Spotflow Metrics Feature - Design Documentation

## Overview

This directory contains the complete architectural and design specifications for the Spotflow SDK Metrics feature. The metrics feature enables embedded devices running Zephyr RTOS to collect, aggregate, and report time-series measurements to the Spotflow observability platform.

**Feature Status**: Design Phase (Ready for Implementation)

**Target SDK Version**: Spotflow Device SDK v2.0+

**Supported Platforms**: Zephyr RTOS 4.1.x, 4.2.x, 4.3.x

## Document Structure

### Core Design Documents

1. **[architecture.md](architecture.md)** - Complete Architecture Specification
   - System requirements (functional, non-functional, integration)
   - Architectural patterns and component design
   - Data models and message formats
   - Risk analysis and mitigation strategies
   - Open questions and design decisions
   - **Read this first** for system-level understanding

2. **[api_specification.md](api_specification.md)** - Public API Reference
   - Complete API surface with signatures and documentation
   - Data types and enumerations
   - Usage examples and best practices
   - Error handling patterns
   - Thread safety guarantees
   - **Reference this** when designing applications using metrics

3. **[ingestion_protocol_specification.md](ingestion_protocol_specification.md)** - Wire Protocol
   - CBOR message format specification
   - Property keys and value types
   - Message structure for all metric types
   - Transport layer (MQTT) configuration
   - Protocol behavior and error handling
   - **Reference this** when implementing encoding or backend services

4. **[implementation_guide.md](implementation_guide.md)** - Developer's Implementation Guide
   - File-by-file implementation instructions
   - Code examples and patterns
   - Testing strategy and test cases
   - Common pitfalls and debugging tips
   - Deployment and rollout guidance
   - **Follow this** when implementing the feature

### Reference Materials

5. **[specification/api.md](specification/api.md)** - Product Owner's API Sketch (IMMUTABLE)
   - Original high-level API requirements
   - Provided by Product Owner
   - **Do not modify** - reference only

6. **[specification/ingestion_protocol.md](specification/ingestion_protocol.md)** - PO Protocol Spec (IMMUTABLE)
   - Original ingestion protocol requirements
   - Provided by Product Owner
   - **Do not modify** - reference only

## Quick Start for Different Roles

### For SDK Architects / Reviewers

**Read in this order**:
1. Start with `architecture.md` - Section 1 (Requirements) and Section 2 (Architecture Diagram)
2. Review Section 3 (Components) for high-level design
3. Check Section 7 (Open Questions) for unresolved decisions
4. Skim `api_specification.md` for API ergonomics
5. Review `ingestion_protocol_specification.md` for backend compatibility

**Focus areas**:
- Alignment with existing SDK patterns (logs, coredumps)
- Resource constraints (memory, CPU, network)
- Thread safety and concurrency
- Error handling and resilience

### For SDK Implementers

**Read in this order**:
1. Skim `architecture.md` for context (especially Section 3-4)
2. Study `api_specification.md` thoroughly - this is your contract
3. Follow `implementation_guide.md` step-by-step
4. Reference `ingestion_protocol_specification.md` when implementing CBOR encoding
5. Refer back to `architecture.md` when making design decisions

**Focus areas**:
- Detailed implementation steps in `implementation_guide.md`
- Code patterns and examples
- Testing requirements
- Performance targets

### For Application Developers (Future Users)

**Read in this order**:
1. Start with `api_specification.md` - Overview and Data Types
2. Study the example scenarios (Section 6)
3. Review best practices (Naming Conventions, Cardinality Management)
4. Reference the complete API reference as needed

**Focus areas**:
- How to register and report metrics
- Label design and cardinality limits
- Error handling patterns
- Integration with existing SDK features

### For Backend/Cloud Developers

**Read in this order**:
1. Read `ingestion_protocol_specification.md` completely
2. Review `architecture.md` Section 4 (Data Models)
3. Check Appendix B in protocol spec for backend processing pipeline

**Focus areas**:
- CBOR message structure and decoding
- Message type discrimination
- Session correlation
- Database schema design

## Key Design Principles

The metrics feature follows these core principles:

1. **Consistency with Existing SDK**: Mirrors patterns from logs and coredumps
   - Backend → CBOR → Queue → Network Processor → MQTT
   - Similar Kconfig structure and naming conventions
   - Shared MQTT connection and processor thread

2. **Resource Efficiency**: Designed for embedded constraints
   - Configurable limits on metrics, time series, queue depth
   - CBOR encoding with integer keys for bandwidth efficiency
   - Lock-free or minimal-locking on fast paths
   - Predictable memory footprint

3. **Reliability**: Graceful degradation under stress
   - Queue overflow drops oldest metrics (not newest)
   - LRU eviction for time series when limits exceeded
   - Metrics failures never crash application
   - Comprehensive error reporting

4. **Extensibility**: Room for future enhancements
   - Sample collection interface (deferred to v2)
   - Custom aggregation functions (future)
   - Cloud-based metric configuration (future)
   - Backward-compatible protocol design

5. **Developer Experience**: Simple API for common cases
   - `spotflow_register_metric_simple()` for basic counters
   - Clear error codes and logging
   - Comprehensive documentation and examples
   - Consistent with Zephyr conventions

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────────────┐
│  Application Code                                            │
│  - Register metrics during init                              │
│  - Report metric values during operation                     │
└─────────────────────┬───────────────────────────────────────┘
                      │ spotflow_metrics.h API
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  Spotflow Metrics Backend                                    │
│  - Metric Registry (name → handle)                           │
│  - Aggregator (sum, count, min, max per time series)        │
│  - CBOR Encoder (message formatting)                         │
└─────────────────────┬───────────────────────────────────────┘
                      │ Message Queue (k_msgq)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  Spotflow Network Processor                                  │
│  - Poll queue for metric messages                            │
│  - Publish to MQTT (shared connection with logs)             │
│  - Priority: Config > Coredumps > Metrics > Logs             │
└─────────────────────┬───────────────────────────────────────┘
                      │ MQTT QoS 0
                      ▼
              Spotflow Cloud Platform
```

## Key Concepts

### Metrics
A **metric** is a named measurement that tracks a numeric value over time. Examples:
- `cpu_temperature_celsius` - Current CPU temperature
- `http_requests_total` - Count of HTTP requests
- `memory_usage_bytes` - Current memory consumption

### Labels
**Labels** are key-value pairs that identify specific time series within a metric. Examples:
- `cpu_temperature_celsius{core="0"}` vs `cpu_temperature_celsius{core="1"}`
- `network_bytes_total{interface="eth0", direction="rx"}`

Each unique combination of label values creates a separate **time series**.

**Limits**:
- Maximum 8 labels per metric
- Label key max length: 16 characters
- Label value max length: 32 characters

### Aggregation
Metrics are **aggregated** over time windows (1 minute, 10 minutes, 1 hour) before transmission to reduce network bandwidth. Aggregation computes:
- **Sum**: Total of all reported values
- **Count**: Number of values reported
- **Min**: Minimum value in window
- **Max**: Maximum value in window

**Average** is derived by cloud: `avg = sum / count`

### Message Format
Metrics are encoded in **CBOR** (binary JSON-like format) for efficiency:
- Integer keys (0x00, 0x10, 0x13...) instead of string keys ("messageType", "metricName", "sum"...)
- Compact encoding: ~50% smaller than JSON
- Self-describing and standardized (RFC 8949)

## Resource Requirements

### Memory Footprint (Typical Configuration)

| Component | Per-Instance | Default Count | Total |
|-----------|--------------|---------------|-------|
| Metric Registry Entry | 128 bytes | 32 metrics | 4 KB |
| Time Series State | 96 bytes | 4 per metric | 12 KB (avg) |
| Message Queue | 8 bytes | 16 slots | 128 bytes |
| CBOR Buffer | 512 bytes | 1 (stack) | 512 bytes |
| **Total (Typical)** | | | **~8 KB** |

**Configurable via Kconfig**: All limits are adjustable for your application's needs.

### CPU Overhead

- Metric registration: ~1 ms (one-time during init)
- Metric reporting: <50 µs typical, <100 µs p99
- Aggregation timer: <10 ms per window expiration
- CBOR encoding: ~200 µs per message
- MQTT transmission: Handled by existing processor thread

**Impact**: Negligible for typical embedded applications (<1% CPU usage at 100 metrics/sec)

### Network Bandwidth

Example traffic (1-minute aggregation):

| Metrics | Time Series Each | Messages/Min | Bytes/Min | Bytes/Day |
|---------|------------------|--------------|-----------|-----------|
| 10 simple | 1 | 10 | 800 | 1.1 MB |
| 5 dimensional | 4 | 5 | 1,200 | 1.7 MB |
| Mixed (typical) | avg 2 | 10-20 | 1,000-2,000 | 1.4-2.8 MB |

**Comparison**: Logs typically generate 10-100 MB/day, so metrics add 1-3% overhead.

## Implementation Timeline

**Estimated Effort**: 6-8 developer-weeks

| Phase | Duration | Deliverables |
|-------|----------|--------------|
| Phase 1: Core Infrastructure | 2 weeks | Registry, aggregation, simple metrics |
| Phase 2: Labeled Metrics | 2 weeks | Multi-timeseries, labels |
| Phase 3: Network Integration | 1 week | MQTT integration, polling |
| Phase 4: Configuration & Docs | 1 week | Kconfig, samples, docs |
| Phase 5: Testing & Validation | 2 weeks | Tests, benchmarks, cloud validation |

## Testing Requirements

### Unit Tests (Required)
- Component-level tests for registry, aggregator, encoder
- Target: >90% code coverage
- Framework: Zephyr ztest

### Integration Tests (Required)
- End-to-end flow from registration to transmission
- Multi-threaded concurrent reporting
- Network failure scenarios

### Performance Benchmarks (Required)
- Metric reporting latency (p50, p99)
- Memory usage under load
- Throughput (metrics/second)
- Must meet targets in architecture spec

### Cloud Integration Validation (Required)
- Live MQTT broker testing
- Backend ingestion verification
- Message format compliance
- Long-duration stability (24+ hours)

## Known Limitations & Future Work

### V1 Limitations

1. **No Sample Collection**: Individual values not stored (only aggregates)
2. **No Persistent Storage**: Metrics lost on device reboot
3. **QoS 0 Only**: No guaranteed delivery (acceptable for telemetry)
4. **No Dynamic Registration**: Metrics should be registered at init
5. **No On-Device Queries**: Cannot read metric values on device

### Planned for V2+

- Sample collection for histograms/percentiles
- Metric filtering and control from cloud
- Custom aggregation functions
- Persistent metrics across reboots
- Prometheus exposition format
- On-device metric value retrieval

## Common Questions

**Q: Can I use metrics in interrupt context?**
A: Yes, but only for simple metrics. Labeled metrics may block briefly.

**Q: What happens if I exceed the time series limit?**
A: New label combinations are rejected with `-ENOSPC` error. Configure `max_timeseries` appropriately at registration.

**Q: How do I know if metrics are being dropped?**
A: Check `dropped_reports` in `spotflow_get_metric_stats()` and SDK logs.

**Q: Can I change a metric's aggregation interval at runtime?**
A: Yes, via `spotflow_set_metric_aggregation_interval()`, but not recommended.

**Q: Are metrics transmitted when offline?**
A: No, they're lost when queue fills. Persistent metrics planned for V2.

**Q: How do I test locally without cloud?**
A: Use Wireshark to capture MQTT traffic, or mock MQTT client in tests.

## Related Resources

### External Documentation
- [Spotflow Platform Docs](https://docs.spotflow.io)
- [Zephyr RTOS Documentation](https://docs.zephyrproject.org)
- [CBOR Specification (RFC 8949)](https://datatracker.ietf.org/doc/html/rfc8949)
- [MQTT v3.1.1 Specification](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html)

### SDK Components
- Spotflow Logs: `modules/lib/spotflow/zephyr/src/logging/`
- Spotflow Coredumps: `modules/lib/spotflow/zephyr/src/coredumps/`
- Spotflow Network: `modules/lib/spotflow/zephyr/src/net/`

### Tools
- [cbor.me](https://cbor.me) - Online CBOR decoder
- [zcbor](https://docs.zephyrproject.org/latest/services/serialization/zcbor.html) - Zephyr CBOR library

## Document Maintenance

**Owners**: Spotflow SDK Architecture Team

**Review Cycle**: Update during implementation as design evolves

**Version Control**: All design docs tracked in Git alongside SDK code

**Feedback**: Open issues or contact SDK team for clarifications/corrections

## Change Log

| Date | Version | Changes |
|------|---------|---------|
| 2025-12-05 | 1.0.0 | Initial design complete, ready for implementation |

## License

Copyright (c) Spotflow.io. All rights reserved.

These design documents are proprietary to Spotflow and intended for internal use by SDK development team.
