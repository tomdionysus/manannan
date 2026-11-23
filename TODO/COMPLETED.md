# Manannan: the record

Newest first. Each entry: what landed, the commit, and its gate counts (tests
run and passed, sanitizers run). Each session leaves a handoff here.

## Handoff, 2026-11-23: plan brought to the group template

- TODO/ACTIVE.md and this file rewritten to the group templates: dated
  numbered decisions, questions to the owner with recommendations, ordered
  work with the test that proves each step and what it waits on, and the
  build-cluster rules relayed by the coordinating session. No code changed.
- Asked the coordinating session for anything Manannan's plan should carry:
  nothing specific, only the standing cluster rules now in the plan.

## Docs, format and CI, 2026-11-12 (8c73c4d to 8e78138, plan dfc5518)

- clang-format (stack dialect, Google 160) applied to the tree (25893bc) and
  checked in CI (8c73c4d). Fixed a race in the test HTTP server that CI's
  coverage build hit (31b1817).
- README cut down to install and set-up; configuration, running, building and
  packaging split into docs/ (f0e0af2, 8e78138). Plan, record and release
  steps added (dfc5518).
- Gates: CI green on main (format, linux with -Werror, ASan+UBSan and
  coverage, clang, arm64 package with lintian, macOS); 59 tests passing.

## 0.3.0: tests, CI, packaging, hardening, 2026-11-12 to 2026-11-13 (tag v0.3.0, packages from 4ba2bc0)

- Fixed a use after dlclose in duplicate plugin detection (a22f943).
- Hardened the systemd unit: no capabilities, private users, devices and IPC,
  hidden /proc, syscall filter @system-service minus @privileged and
  @resources, W^X. systemd-analyze security 7.0 MEDIUM to 1.1 OK (5ace290).
- GoogleTest suite over a manannan_core library: config, duration, loggers,
  the plugin loader against purpose-built bad plugins, the service loop with
  real signals, the application entry point and the daemon binary, checkip
  and Route 53 against a loopback HTTP server, SigV4 against vectors computed
  independently. `--version`, a generated version header,
  `Manannan/<version>` user agent, a manannan(8) page, http:// or https:// on
  the Route 53 endpoint (ec54047).
- GitHub CI, Codecov, CodeQL, Dependabot (7e19c9b); all four initial
  Dependabot bumps merged.
- Debian packaging (manannan, manannan-dev, manannan-dbgsym) built by
  scripts/build-deb.sh with the version from git describe; postinst creates a
  root-only environment file; purge removes /etc/manannan (4ba2bc0).
- Published 0.3.0 in the apt repository, built from the tag on a trixie arm64
  Pi. Both production nodes moved from /usr/local source installs to the
  package, with a full cycle checked at debug on each.
- Repository renamed on GitHub from "mannanan" to "manannan"; URLs updated.
- Gates: 59 tests run, 59 passed, in the package build and in CI; ASan+UBSan
  clean in CI; line coverage above 99%; lintian clean.

## 0.2.0: multiple records, 2026-11-01 (bdd0ef7, tag v0.2.0)

- Registries manage any number of records (plugin ABI 2); each record is
  reconciled independently. aws_route53 takes a records list with per-record
  zone, TTL and credentials; credentials cascade to the AWS_* environment.
- Fixes: tests compiled out under Release (assert), missing-root detection,
  credentials fallback, exceptions rethrown after unload, dynamic_cast across
  RTLD_LOCAL, case-insensitive and octal-escaped record names, xml_unescape
  order, duration overflow, empty LOG_LEVEL, duplicate plugin directories,
  curl global cleanup.
- Install paths generated from the CMake install directories; an existing
  configuration is never overwritten; yaml-cpp 0.6 to 0.8.
- Keys moved from the configuration to the environment file. Installed on two
  nodes.
- Gates: not recorded (before the test suite).

## 0.1.0: initial version, 2026-09-11 (b57e755)

- The daemon, the plugin loader, amazon_check_ip and aws_route53 with SigV4,
  a systemd unit, macOS build support (5443283), logging with LOG_LEVEL
  (5017703), docs/NAME.md.
- Gates: not recorded.
