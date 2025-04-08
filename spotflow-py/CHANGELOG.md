# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.1.0] - 2025-04-09

### Added

- The ability to allow remote access to TCP ports on the device.

### Fixed

- Disabled retrying after non-recoverable error 400 during provisioning operation initialization.
- Old reported and desired properties are not kept in the local database.

## [2.0.4] - 2024-06-26

### Fixed

- `wait_enqueued_messages_sent` and `send_message` can be canceled using Ctrl+C.

## [2.0.3] - 2024-06-17

### Added

- The list of supported operating systems and CPUs is in the documentation.

## [2.0.2] - 2024-05-03

### Added

- Automatic publishing of the package in CI/CD.

### Fixed

- The synchronization of the Desired Properties now works correctly even after an MQTT reconnection or when the version number is out of sync.

## [2.0.1] - 2024-04-30

### Added

- The metadata of the PyPI package now contain important links.

## [2.0.0] - 2024-04-29

### Added

- The interface for synchronous sending of Messages.

### Changed

- The parameters in `DeviceClient.start` are now ordered by their decreasing importance.
- The names of the methods for enqueueing Messages are now more consistent.
