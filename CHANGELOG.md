# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased
### Added
* Added support for Zephyr v4.2.0

## [0.6.0] - 2025-10-23
### Added
* Added ESP-IDF module with support for logging
* Added ability to remotely configure the minimal severity of sent log messages in Zephyr
### Fixed
* Fixed coredump example in debug mode on Nordic boards in Zephyr

## [0.5.1] - 2025-09-25
### Added
* Added support for parsing latest Let's Encrypt certificates on Nordic boards

## [0.5.0] - 2025-08-26
### Added
* Added coredumps functionality
* Obtaining device ID from Zephyr if not provided in Kconfig
* Support for overriding device ID in code
* Computing build ID and sending it in session metadata and core dumps
### Changed
* Changed KConfig option LOG_BACKEND_SPOTFLOW SPOTFLOW_LOG_BACKEND to match other options
* Updated default kconfig values to be more stable

## [0.4.0] - 2025-07-23
### Fixed
* Fixed logs memory leak

## [0.3.0] - 2025-07-09
### Changed
* Used mbedtls heap by default to prevent heap fragmentation and certificate chain validation issues

## [0.2.0] - 2025-07-01
### Added
* Added propagation of the message sequence number to the message metadata
### Fixed
* Unable to process DHCP offer in Zephyr v4.1.0
* File descriptor exhaustion in Zephyr v4.1.0

## [0.1.0] - 2025-06-25
### Added
* Added initial implementation of Spotflow Device SDK
* Added sample for NXP Board FRDM-RW612
* Added sample for Nordic board NRF7002DK
* Added sample for Espressif board ESP32-C3-DevKitC
