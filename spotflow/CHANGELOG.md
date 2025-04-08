# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.8.0] - 2025-04-09

### Added

- The ability to allow remote access to TCP ports on the device.

### Fixed

- Disabled retrying after non-recoverable error 400 during provisioning operation initialization.
- Old reported and desired properties are not kept in the local database.

## [0.7.0] - 2024-06-26

### Added

- `wait_enqueued_messages_sent` and other synchronous methods periodically call `check_signals` on the provided `ProcessSignalsSource` so that they can be canceled using Ctrl+C.

### Changed

- `ProcessSignalsSource` is now required to be `Send` and `Sync`.

## [0.6.1] - 2024-06-11

### Fixed

- The generation of the database files is now skipped during the documentation generation.

## [0.6.0] - 2024-06-11

### Added

- The whole public interface is now documented.
- The crate was published on crates.io.

### Changed

- The names of some of the structures and functions in the public interface were changed to be more intuitive.
- Multiple crates were merged into a single one to increase the stability of the interface.
