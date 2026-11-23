# Manannan: the record

Newest first. Each entry: what landed, the commit, and its gate counts (tests
run and passed, sanitizers run). Each session leaves a handoff here.

## Alignment steps 2 to 6, 2026-11-23 (develop 922269a to dd825c0)

- develop created from main. Each step a feature branch squashed onto it.
- Layout (922269a): the plugin interface under manannan/include (all that
  manannan-dev now installs: plugin.h, loggers/logger.h, version.h), the
  daemon in manannan/daemon (namespace manannan::daemon), the loggers in
  manannan/loggers, the tests in manannan/test named by subsystem;
  executables at the top of the build tree.
- Private members `_name` (b44540a), test fixtures included.
- Header block on every source, CMake file, script and YAML file; CMake
  3.24 (Ubuntu 22.04 leaves the source-build list); -Wthread-safety under
  clang (49130f0).
- tools/ci.sh with the group's steps and a fuzz step; the template
  workflow with concurrency and paths-ignore, x86-64 and arm64 jobs, deb on
  both; CodeQL on main and weekly; codecov.yml ignores; release badge; bare
  tags in tools/build-deb.sh; a package consumer (manannan/test/package)
  that builds a plugin against the installed headers (dd825c0).
- Gates on a claimed Pi: release (g++) and clang-19 builds with -Werror, 59
  tests run, 59 passed, each step; tools/ci.sh check (seen red on a missing
  header first), release, clang, tidy, asan (59 passed), package (consumer
  OK), coverage 99.2% of lines (636 of 641), 97.7% of functions (85 of 87).
  The fuzz step waits on step 10; tsan runs at the release gate.

## Alignment step 1: identifying data out of the tree and history, 2026-11-23

- The Route 53 tests used real record names; they now use example names.
  History was rewritten with git filter-repo (replace-text, four literal
  rules) so no commit carries them. The fix commit itself became empty in the
  rewrite and was dropped: the example names now appear from 0.2.0 on.
- Old to new hashes for every commit after 5017703: bdd0ef7 to 4e5730c
  (0.2.0), a22f943 to 9bb4a00, 5ace290 to 6de3749, ec54047 to 874bba2,
  7e19c9b to 9c7ae6d, 4ba2bc0 to 8d041e7 (the published 0.3.0 packages),
  f0e0af2 to 258e6b8 (0.3.0), 25893bc to c86a41c, 8c73c4d to d19ffa3,
  31b1817 to 86ce71c, 8e78138 to 14492e1, dfc5518 to 65ad724, 2cf037f to
  fe11517. The full map and a bundle of every ref before the rewrite are in
  build/history-rewrite/ (untracked).
- Tags v0.2.0 and v0.3.0 follow the rewritten commits; bare 0.2.0 and 0.3.0
  added beside them. The stale dependabot branch deleted locally.
- Not yet pushed: main and all tags need a force-push on the owner's word.
  GitHub keeps refs/pull/1-4/head and their objects; only GitHub support can
  purge them.
- Gates: release build with -Werror on a claimed Pi, 59 tests run, 59 passed.

## Handoff, 2026-11-23: plan brought to the group template

- TODO/ACTIVE.md and this file rewritten to the group templates: dated
  numbered decisions, questions to the owner with recommendations, ordered
  work with the test that proves each step and what it waits on, and the
  build-cluster rules relayed by the coordinating session. No code changed.
- Asked the coordinating session for anything Manannan's plan should carry:
  nothing specific, only the standing cluster rules now in the plan.

## Docs, format and CI, 2026-11-12 (d19ffa3 to 14492e1, plan 65ad724)

- clang-format (stack dialect, Google 160) applied to the tree (c86a41c) and
  checked in CI (d19ffa3). Fixed a race in the test HTTP server that CI's
  coverage build hit (86ce71c).
- README cut down to install and set-up; configuration, running, building and
  packaging split into docs/ (258e6b8, 14492e1). Plan, record and release
  steps added (65ad724).
- Gates: CI green on main (format, linux with -Werror, ASan+UBSan and
  coverage, clang, arm64 package with lintian, macOS); 59 tests passing.

## 0.3.0: tests, CI, packaging, hardening, 2026-11-12 to 2026-11-13 (tags 0.3.0 and v0.3.0, packages from 8d041e7, 4ba2bc0 before the history rewrite)

- Fixed a use after dlclose in duplicate plugin detection (9bb4a00).
- Hardened the systemd unit: no capabilities, private users, devices and IPC,
  hidden /proc, syscall filter @system-service minus @privileged and
  @resources, W^X. systemd-analyze security 7.0 MEDIUM to 1.1 OK (6de3749).
- GoogleTest suite over a manannan_core library: config, duration, loggers,
  the plugin loader against purpose-built bad plugins, the service loop with
  real signals, the application entry point and the daemon binary, checkip
  and Route 53 against a loopback HTTP server, SigV4 against vectors computed
  independently. `--version`, a generated version header,
  `Manannan/<version>` user agent, a manannan(8) page, http:// or https:// on
  the Route 53 endpoint (874bba2).
- GitHub CI, Codecov, CodeQL, Dependabot (9c7ae6d); all four initial
  Dependabot bumps merged.
- Debian packaging (manannan, manannan-dev, manannan-dbgsym) built by
  scripts/build-deb.sh with the version from git describe; postinst creates a
  root-only environment file; purge removes /etc/manannan (8d041e7).
- Published 0.3.0 in the apt repository, built from the tag on a trixie arm64
  Pi. Both production nodes moved from /usr/local source installs to the
  package, with a full cycle checked at debug on each.
- Repository renamed on GitHub from "mannanan" to "manannan"; URLs updated.
- Gates: 59 tests run, 59 passed, in the package build and in CI; ASan+UBSan
  clean in CI; line coverage above 99%; lintian clean.

## 0.2.0: multiple records, 2026-11-01 (4e5730c, tags 0.2.0 and v0.2.0)

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
