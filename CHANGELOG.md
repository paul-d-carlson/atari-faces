# Changelog

All notable changes to this project are documented here. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
the project uses [Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-11-23

First public release.

### Added

- An eigenfaces face recognition demo for the 48K Atari 800, in portable
  C89 for cc65: it trains on ten faces from the Olivetti database,
  showing the mean face, the covariance matrix, the eigenvectors (by
  power iteration) and the eigenfaces on a GTIA mode 9 screen, then
  recognizes five faces it has never seen.
- A single make-based build: `make` for `faces.xex`, `make atr` for a
  bootable DOS 2.5 disk, `make deps` to fetch the pinned, checksummed
  third-party tools, and `make data` to regenerate the face data and its
  numpy reference results.
- A build-time guard that fails if the load address is not above the
  DOS 2.5 MEMLO, a checksum check on the bundled DOS, and a report of the
  RAM left free after every link.
- Unit tests that run both natively, with AddressSanitizer,
  UndefinedBehaviorSanitizer and 16-bit overflow checks, and on the
  emulated 6502 under libatari800 with line coverage; a reference suite
  that compares the integer math with a float PCA; an integration test
  that boots the real demo disk and checks every match, the timing
  budgets, the sound effects and the C stack; and tests for the data
  exporter.
- GitHub Actions CI on Ubuntu 24.04: builds, runs every test suite,
  reports free memory, timings and coverage, and publishes a release
  archive for `v*` tags.

[0.1.0]: https://github.com/paul-d-carlson/atari-faces/releases/tag/v0.1.0
