# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.2.1] - 2025-06-02

### Changed

- Updated links to documentation.

## [2.2.0] - 2025-04-09

### Added

- The ability to allow remote access to TCP ports on the device.

### Fixed

- Disabled retrying after non-recoverable error 400 during provisioning operation initialization.
- Old reported and desired properties are not kept in the local database.

## [2.1.1] - 2024-06-17

### Fixed

- Links to the official documentation from the public interface now point to the correct locations.

## [2.1.0] - 2024-05-24

### Added

- Interface for the remote configuration of the Device.

## [2.0.1] - 2024-05-06

### Added

- Automatic publishing of the library in CI/CD.

## [2.0.0] - 2024-04-29

### Added

- The interface for synchronous sending of Messages.

### Changed

- The parameters in `spotflow_client_options_create` are now ordered by their decreasing importance.
- The names of the methods for enqueueing Messages are now more consistent.
