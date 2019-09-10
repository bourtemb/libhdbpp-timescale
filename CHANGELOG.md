# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Depend on new libhdb++ library as a submodule

## [0.9.2] - 2019-09-10

### Fixed

- Close logging down in destructor so linked device server can be restarted.

### Changed

- Build system correctly produces a major version shared object
- Removed Clang path from build (CMake checks PATH)
- Corrected static library build
- Install now places header in include/hdb++/

## [0.9.1] - 2019-07-18

### Added

- Google benchmark sub-module for micro benchmarking. This will enabled future optimisation
  - Benchmark tests for QueryBuilder

### Fixed

- Fix to build system - can build without Clang enabled now.
- Updates/corrections to docs

## [0.9.0] - 2019-07-12

### Added

- Pre v1 release of the library. 
  - This is an almost complete version of the library. 
  - Next couple of versions will complete the library before a v1 release.
