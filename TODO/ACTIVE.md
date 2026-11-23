# Manannan: the plan

## Resume here

The alignment to the group standards (Decisions 9 to 15,
TODO/2026-11-23-alignment.md) is done on `develop`, which carries the 0.4.0
version commit; every local release gate has passed once (COMPLETED.md).
What remains of the release waits on the owner's word to push (below):
nothing is pushed, history was rewritten locally, and main and every tag
need a force-push. After it: hosted CI green on develop, then merge develop
to main, tag 0.4.0 on main, build the packages from the tag on a claimed
trixie arm64 Pi, and stage them in the apt tree's incoming/ (Decision 8);
the owner uploads; nodes upgrade on the owner's go, checking the migration
at debug on each. No build-cluster cores are held. Builds run through
build/pi-test.sh and build/pi-ci.sh (untracked helpers: they rsync the tree
to /root/manannan-work on a claimed Pi). 0.3.0 is what the apt repository
and both nodes run.

## Principles

- A small, dependable daemon: one source plugin, one registry plugin, each
  record reconciled on its own. Nothing is added that a node does not need.
- No network in tests: checkip and Route 53 are a loopback server; SigV4 is
  checked against independently computed vectors with the time injected.
- Every reader of outside bytes is an untrusted-input parser with a fuzz
  target. Every retry has backoff, a budget and a visible parked state
  (self-healing disciplines, cited from the group's principles and laws).
- A published version is never rebuilt; the next change is a new version.

## Build cluster (the owner's standing rules, via the coordinating session)

- All builds and tests run on a claimed build-cluster Pi, fuzzing included
  (Decision 7).
- Any claimed Pi will do; there is no fixed release machine. Check its OS
  release before a package build. Claim only the cores `-j` uses, nice 19,
  one step at a time, release the moment a step ends. When none are free,
  `claim-board wait` in the background naming several machines, never poll.
- ASan and UBSan run on any Pi. TSan works on one Pi only (clang 19,
  Makefiles; which one is in CLAUDE.local.md). Sanitizers run at a feature or
  milestone end, before a merge to develop, or to chase a bug.
- Test once: a test that fails at random is a finding, never a rerun.
- A missing -dev package is installed on a Pi with one claimed core, nice 19,
  and a board note naming the package and the machine. A note goes on the
  board before anything disruptive.
- A Pi that drops out is normal: release, rerun once elsewhere.

## Decisions

1. 2026-11-12: packages only for Debian trixie arm64; other platforms build
   from source.
2. 2026-11-01: the configuration is YAML through yaml-cpp; secrets never in
   the main configuration file. (The environment file is superseded by
   Decision 12.)
3. 2026-11-12: the service ships disabled; the packaged configuration is an
   example.
4. Superseded by Decision 10: tags were `v`-prefixed up to v0.3.0.
5. 2026-11-12: README is install-and-set-up only; everything else lives in
   docs/.
6. 2026-11-12: the source tree uses the stack dialect (Google, 160 columns),
   checked in CI.
7. 2026-11-23: "Build on the cluster. Use the coordinator rules." Every build
   and test runs on a claimed build-cluster Pi, never the Mac, under the
   rules in "Build cluster" below.
8. 2026-11-23: by the same ruling, minor and patch releases are cut without
   asking once their tasks are done: tag, build, stage in the apt tree's
   incoming/. A major bump is the owner's. Pushing and the apt upload remain
   the owner's.
9. 2026-11-23: identifying data (the real record names used as test data) is
   removed from the tree and from history by a rewrite, then force-pushed.
10. 2026-11-23: the group git flow: develop, feature branches squashed onto
    it, main only at a release. Bare `x.y.z` tags from 0.4.0; bare 0.2.0 and
    0.3.0 added on the rewritten commits beside the v tags.
11. 2026-11-23: the library layout (manannan/include, manannan/<subsystem>,
    manannan/test, manannan/fuzz, tools/) and `_name` private members.
12. 2026-11-23: the group configuration and packaging: config.yaml and
    secrets.yaml (0600), `--a-b` and `MANANNAN_A_B` overrides, `log.level` and
    `log.format`, /usr/bin, a manannan system user; postinst migrates the old
    files. A breaking change, taken without backward compatibility.
13. 2026-11-23: an injected clock and wait in the service loop; a Secret type
    for credentials; `-Wthread-safety` under clang.
14. 2026-11-23: the full group CI set and project files (CONTRIBUTING,
    CODE_OF_CONDUCT, CHANGELOG, docs/licensing.md, header block everywhere,
    CMake 3.24, MANANNAN_BUILD_FUZZING).
15. 2026-11-23: the alignment, fuzz targets and back-off are 0.4.0; AAAA is
    0.5.0.

## Waiting on the owner

1. Push, with force for the rewritten history: `git push --force origin main
   0.2.0 0.3.0 v0.2.0 v0.3.0` (main and the four tags), `git push origin
   develop`, and delete the stale remote branch
   dependabot/github_actions/actions/checkout-7. The workflows changed, and the
   gh token has no workflow scope, so this goes over ssh. Recommendation: push.
2. GitHub keeps refs/pull/1-4/head with the old commits, and the record
   names in them; only GitHub support can remove those. Recommendation: ask
   them, since the names are already public DNS names.
3. After the release is staged: the apt upload, then each node's upgrade.

## The work, in order

1. Finish 0.4.0 after the push (Resume here). The alignment steps are all
   done; TODO/2026-11-23-alignment.md is their record.

Then for 0.5.0: AAAA records (an IPv6 source and a record-type setting; a
plugin interface change bumps `plugin_abi_version`). Waits on the owner
choosing the IPv6 source.
