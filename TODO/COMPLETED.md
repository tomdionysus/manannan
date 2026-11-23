# Completed

The record, newest first.

## 0.3.0: tests, CI, packaging, hardening (2026-11-12 to 2026-11-13)

- Fixed a use after dlclose in duplicate plugin detection (the error built its
  message from the unloaded descriptor's name).
- Hardened the systemd unit: no capabilities, private users, devices and IPC,
  hidden /proc, syscall filter @system-service minus @privileged and
  @resources, W^X. systemd-analyze security 7.0 MEDIUM to 1.1 OK.
- GoogleTest suite of 59 tests over a manannan_core library: config, duration,
  loggers, the plugin loader against purpose-built bad plugins, the service
  loop with real signals, the application entry point and the daemon binary,
  checkip and Route 53 against a loopback HTTP server, SigV4 against vectors
  computed independently. Line coverage above 99%.
- `--version`, a generated version header, `Manannan/<version>` user agent, a
  manannan(8) page, http:// or https:// on the Route 53 endpoint.
- GitHub CI (format, linux with ASan/UBSan and coverage, clang, arm64 package
  with lintian, macOS), Codecov, CodeQL, Dependabot. All four initial
  Dependabot bumps merged.
- Debian packaging (manannan, manannan-dev, manannan-dbgsym) built by
  scripts/build-deb.sh with the version from git describe; postinst creates
  a root-only environment file; purge removes /etc/manannan.
- Published 0.3.0 in the tomcully.com apt repo (built from the tag on a trixie
  arm64 machine, lintian clean, 59 tests passing in the build).
- Moved both production nodes from /usr/local source installs to the package
  and checked a full cycle at debug on each.
- clang-format (stack dialect, Google 160) applied to the tree and checked in
  CI. Fixed a race in the test HTTP server that CI's coverage build hit.
- README cut down to install and set-up; configuration, running, building and
  packaging split into docs/.
- Repository renamed on GitHub from "mannanan" to "manannan"; URLs updated.

## 0.2.0: multiple records (2026-11-01 to 2026-11-03)

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
  nodes, with example.org added to the first.

## 0.1.0: initial version

- The daemon, the plugin loader, amazon_check_ip and aws_route53 with SigV4,
  a systemd unit, macOS build support, docs/NAME.md.
