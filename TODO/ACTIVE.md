# Manannan: the plan

## Resume here

0.4.0 is released and running: the apt repository serves it and both
production nodes run it, migrated and checked (TODO/COMPLETED.md). The
0.4.x line takes fixes only until 0.5.0.

0.5.0 is the design in TODO/2026-11-23-clustered-dns-spec.md (section 13
the build order, section 15 the owner's answers). The section 14 requests
went to the sibling projects on the owner's go; their answers are below.
The DNS wire format (manannan/dns) is on develop, ahead of the order
because it needs nothing from the stack. Step 1 (on the stack) waits for
Dagda 0.5.0; Dannan 0.12.0 is tagged.

## Principles

- Every 0.4 function keeps working through every change: discovery of the
  public address, records kept upstream, the plugin interface, the retry
  discipline, secrets never logged (the spec's R1 to R7).
- No special nodes. One node is a cluster of one; any node can do any job.
- Answering never waits: queries are served from an in-memory snapshot,
  never from the store or the signer.
- No network in tests: outside services are loopback servers, time is
  injected and stepped, signatures are checked against independent vectors.
- Every reader of outside bytes is bounded and has a fuzz target. Every
  retry has back-off, a budget, a visible parked state and an operator
  action (the self-healing disciplines).
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
- No preferred nodes (2026-11-23): take whichever Pi has the cores free
  now, rsync the tree there and build from scratch. Never queue for a Pi
  because it holds a warm tree. Install a missing tool or -dev package
  yourself (one core, nice 19, a board note). Wait only when no machine has
  the cores, naming every machine. The one exception is a job that must run
  on one machine (TSan on the clang 19 Pi); the claim's job text says so.

## Decisions

1. 2026-11-12: packages only for Debian trixie arm64. From 0.5.0 arm64 and
   x86-64 (Decision 17).
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
15. 2026-11-23: the alignment, fuzz targets and back-off are 0.4.0. (AAAA,
    once planned alone for 0.5.0, is part of the 0.5.0 design.)
16. 2026-11-23: no GitHub Support request about the pull-request refs that
    still hold pre-rewrite commits ("Do NOT do 3"). Not to be raised again.
17. 2026-11-23: 0.5.0 is the design in TODO/2026-11-23-clustered-dns-spec.md.
    Its section 15 lists the owner's answers, each a decision of this plan.
    Among them: Manannan owns its DNS server, Dannan depends on Manannan in
    nothing and no project relies on it for resolution; configuration and
    zones live in Dagda (superseding Decision 12's files for 0.5.0); one
    release, no stepped versions; the two production nodes join one cluster.
18. 2026-11-23: never use the browser or any tool acting with the owner's
    signed-in accounts unless he asks for browser use in that message (his
    global rule). Authenticated CLIs stay within the named repository and
    task.
19. 2026-11-24: the registrar credential stays as the spec has it, opened
    only by the holder of the domain's writer lease. 0.5.0's registrar
    plugins and the release wait for Dagda's key wrapping (after Dagda
    0.5.0), with no interim per-node registrar credential.

## Waiting on the owner

Nothing at present.

## What the siblings answered (2026-11-23)

- Dannan: reachability (NodeRecord path_class, Nodes::watch), agreement
  (Context::agreement) and adverts and health (NodeRecord::adverts,
  watch_advert, status::health_of from 0.12.0) exist. 0.12.0 is tagged
  (2026-11-24) with the password credential; 0.13.0 (about 12-05) the HTTP
  server core, the DNS client, named-slot placement (a pure function,
  recomputed on Nodes::watch), a true-time interval for signing, a watch
  on revocations, and the external address per family. JSON exists. Each
  new shape comes to Manannan before code. FROST is built in Manannan:
  it moves to Dannan only if a second project needs it.
- Dagda: register maps (D6, context.registers()) are on its develop; leases
  in head spaces with the epoch fence (D5) come with Dagda 0.5.0, in days.
  The registrar's write is a register commit carrying the lease epoch. A
  merge Manannan supplies exists today (heads().open with its own codec and
  merge); watch exists (heads().watch, registers().changed). A node-sealed
  space covers per-node credentials now. Not covered before Dagda 0.5.0: a
  registrar credential only the lease holder can open (key wrapping to the
  holder on takeover); it goes into Dagda's plan after 0.5.0 with Manannan
  waiting, and its shape comes to Manannan first. Until then the spec's
  section 7.3 registrar credential has no home; registrar plugins (step 4)
  and the release wait on it (Decision 19).
- Lugh: everything is at 0.6.0, and Manannan is in its dependants. App
  roles are in the ID token and userinfo, not in access tokens: map roles
  at sign-in, and for an API call with only an access token read the
  user's groups. lugh_client 0.6.0 builds on Dannan 0.11.x. No group-change
  push yet: a role change lands at the next sign-in or groups read.

## The work, in order

1. Done: the DNS wire format (TODO/COMPLETED.md).
2. 0.5.0, in the order of the spec's section 13: on the stack (after
   Dagda 0.5.0; Dannan 0.12.0 is tagged); the DNS server; DNSSEC; registrar plugins; the
   operator's optional features; the API and dashboard. Each is a feature
   branch squashed onto develop, keeping R1 to R7 working. The release
   cannot ship before Dannan's HTTP server and credential helper and
   Dagda's register maps and leases exist.

TODO/2026-11-23-alignment.md is the record of the 0.4.0 alignment, all done.
