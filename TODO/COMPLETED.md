# Manannan: the record

Newest first. Each entry: what landed, the commit, and its gate counts (tests
run and passed, sanitizers run). Each session leaves a handoff here.

## Second history rewrite, 2026-11-23

- A record entry written this day (first committed as 861a244) named a
  production node, a record and the node's public address. On the owner's
  word the entry was rewritten forward and then removed from history with
  git filter-repo (six literal replacements), and main, develop and the
  0.4.0 tag were force-pushed with leases on their old tips.
- Old to new: 861a244 to 36d778a, 3d7c225 to 05798b8 (main, and the 0.4.0
  tag's commit), 55b82a9 to 3a08abd, af4a3de to c61681c, f24b34b to
  d30206d, 9ea2693 to 4e96da4 (the spec), 85cb935 to 417aeaf. Every commit
  before 861a244 is unchanged, and so are the tags before 0.4.0. The map and
  a bundle of every ref before the rewrite are in build/history-rewrite-2/
  (untracked).
- The 0.4.0 tag's tree differs from the one the staged packages were built
  from only in TODO/COMPLETED.md, which is not packaged, so the packages in
  incoming/manannan/0.4.0 stand.
- GitHub still serves the old commits by hash until it collects them.

## Handoff, 2026-11-23: 0.4.0 pushed, the 0.5.0 design settled

- On the owner's "push": main, develop and the 0.4.0 tag pushed,
  both fast-forwards. CI (all jobs) and CodeQL green on main.
- The 0.5.0 design written and settled with the owner question by question:
  TODO/2026-11-23-clustered-dns-spec.md (committed 4e96da4). Manannan
  becomes a clustered authoritative DNS server on Dannan and Dagda, keeping
  every 0.4 function. 32 answers recorded in its section 15; nothing open.
- The design went through a critique before the owner's later rounds. What
  it changed: each node publishes only its own address (no shared publisher
  to split-brain); a restarted node serves its saved zones at once; merges
  end in a deterministic repair rule; DNSSEC keys are per node with a FROST
  threshold key-signing key no node holds, so a stolen node exposes no zone
  key; credentials are per node with least privilege.
- Corrections taken on the way: Dannan depends on Manannan in nothing, and
  reachability is Dannan's (no test of Manannan's own); the DNS needs five
  sibling projects sent in on a coordinator's wrong steer were withdrawn the
  same day, so the spec names no consumer.
- A Chrome attempt to file a GitHub Support request was stopped by the
  owner; the request was then dropped for good (Decision 16) and the owner's
  global rule now forbids browser use unasked (Decision 18).
- PLUGINS.md now describes the 0.4.0 back-off. No code changed.

## 0.4.0 built and staged, 2026-11-23 (main 05798b8, tag 0.4.0)

- On the owner's word: main and the tags 0.2.0, 0.3.0, v0.2.0 and v0.3.0
  force-pushed onto the rewritten history (main with a lease on the old
  tip), develop pushed, the stale dependabot branch deleted; the repository's
  description and topics set. Hosted CI on develop: all 15 jobs green.
- develop merged to main and tagged 0.4.0, locally. Packages built from a
  fresh clone of the tag on a claimed trixie arm64 Pi: 89 tests run, 89
  passed in the build; lintian clean. Staged in the apt tree's
  incoming/manannan/0.4.0/ (sha256 of manannan_0.4.0_arm64.deb begins
  97dd59ad002b).
- The GitHub Support request for the pull-request refs was not filed: a
  browser attempt was stopped and browser use is now the owner's to ask
  for. It is listed under "Waiting on the owner".

## Handoff, 2026-11-23: 0.4.0 gated locally, waiting on the push

- Version 0.4.0 on develop (99e4957). Release gates, each once, on claimed
  Pis: tools/ci.sh check, release (89 run, 89 passed), clang 19 (89/89),
  tidy, asan with UBSan (89/89), package (consumer OK at 0.4.0, plugin ABI
  2), coverage 99.4% of lines (988 of 994) and 97.7% of functions (127 of
  130), fuzz (both targets 60 s, clean); TSan on the clang 19 Pi (89/89)
  after a fix to one test (4ad76ce): under TSan a signal's handler runs at
  the next intercepted call, not inside pselect, so the test read its flag
  too soon (failing under TSan, passing with the check moved; the other
  gates ran before this test-only change).
- Not done: hosted CI, merge to main, tag, package build and staging. They
  wait on the push (ACTIVE.md).
- On the owner's go, one production node's 0.3.0 configuration gained a
  record in a second zone (asked by the SIP server project's session; the
  node, record and backup are in CLAUDE.local.md). After a restart, the
  journal showed the record set to the node's address, and it resolved at
  the zone's name server and publicly. That is also the first
  real record write under the hardened unit, which the plan was waiting to
  observe.

## Alignment steps 7 to 12, 2026-11-23 (develop a416ce6 to 265d11e)

- Injected clock (a416ce6): `Clock` with now() and a signal-aware
  wait_until(); `SystemClock` waits in pselect; the service tests step a fake
  clock and sleep for none of it. A failed wait restores the signal state
  (tested). The cadence test was seen red with the deadline taken before the
  cycle.
- Secret type (0c8a445): AWS secret key and session token in an mmap'd,
  mlock'd, MADV_DONTDUMP page cleansed on release; not printable or
  convertible; SigV4 derives keys in cleansed buffers; a test finds neither
  secret in any log line of a full read and update.
- Configuration (8a994bd): config.yaml plus secrets.yaml (owner-only, merged
  over it), MANANNAN_<KEY> and --<key> overrides, log.level and log.format
  (json lines), --config/--secrets, `manannan migrate`; /usr/bin, a
  manannan system user (sysusers), preinst/postinst migration, rm_conffile.
  Proven on a build machine: the published 0.3.0 package with a 0.3 config
  and environment, upgraded to the new build, migrated (config.yaml,
  secrets.yaml 0600 manannan:manannan, user created), and the hardened unit
  started and loaded its keys; systemd-analyze security 1.1 OK; lintian
  clean; then purged. A yaml-cpp merge by Node assignment lost the manannan
  root under test; the merge is made in place instead.
- Fuzz targets (39cf3df): route53_response and checkip_body under
  manannan/fuzz; the response reader is find-based instead of std::regex;
  an escape such as \999 no longer throws (fixed, tested); responses capped
  at 1 MiB, check IP bodies at 64 bytes (tested). Both targets 60 s clean
  under ASan and UBSan (the response reader: 2,190,414 runs).
- Back-off (65abd37): doubling from one cadence to 64, parked at 8 failures
  in a row (said once), still retried every 64 cadences, at once on a new
  source value, and on SIGHUP (`systemctl reload`). The SIGHUP test was seen
  red without the reset.
- Project files (265d11e): CONTRIBUTING.md, CODE_OF_CONDUCT.md, CHANGELOG.md,
  docs/licensing.md.
- Gates per step on a claimed Pi, tests run and passed: 63, 68 (g++ and
  clang, tidy, ASan), 79 (and the package build's tests), 84, 89.

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
