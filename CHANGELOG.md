# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased
### Added

### Changed
* Used mbedtls heap by default to prevent heap fragmentation and certificate chain validation issues
* Changed KConfig option LOG_BACKEND_SPOTFLOW SPOTFLOW_LOG_BACKEND to match other options
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
