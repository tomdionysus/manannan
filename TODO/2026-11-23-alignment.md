# Alignment with the group standards

The owner reviewed an audit of Manannan against the group standards on
2026-11-23 and chose, area by area, what to adopt. This document records the
gaps, the choices and the order. The plan (ACTIVE.md) carries the steps; this
carries the detail. Everything below lands as 0.4.0, except AAAA, which waits
for 0.5.0.

## The choices

| Area | Today | Chosen |
| --- | --- | --- |
| Identifying data | real record names as test data in tests/route53_test.cpp, and in history since 0.2.0 | replace with example names, and rewrite history |
| Git flow | main only; tags v0.2.0, v0.3.0 | develop, feature branches squashed onto it, main at a release; bare tags from 0.4.0, plus bare 0.2.0 and 0.3.0 on the rewritten commits |
| Layout | include/, src/, tests/, plugins/ at the root | the library layout (below) |
| Private members | `name_private` | `_name` |
| Configuration | /etc/manannan/manannan.yaml; keys in an environment file; LOG_LEVEL from the environment | /etc/manannan/config.yaml and secrets.yaml (0600, made by postinst); every key overridable as `--a-b` and `MANANNAN_A_B`; `log.level`, `log.format` |
| Packaging | /usr/sbin; DynamicUser | /usr/bin; a `manannan` system user via sysusers; postinst migrates the old files |
| Time | the service loop reads steady_clock; tests wait in real time | an injected clock and wait; tests step time |
| Secrets | AWS key and secret as std::string | a Secret type: not printable, locked and cleansed memory |
| Thread safety | none (single-threaded) | `-Wthread-safety` under clang on the warnings target |
| CI | scripts/ci.sh; format, linux, clang, arm64 package, macOS | tools/ci.sh with check, release, clang, tidy, asan, tsan, coverage; the template ci.yml, CodeQL, codecov.yml and badges |
| Files | no CONTRIBUTING, CODE_OF_CONDUCT, CHANGELOG, docs/licensing.md; header block on a few files only; CMake 3.20 | all added; header block everywhere; CMake 3.24; MANANNAN_BUILD_FUZZING |

## Target layout

```
CMakeLists.txt
manannan/CMakeLists.txt            manannan_core, the daemon
manannan/include/manannan/         plugin.h and the other public headers, version.h.in
manannan/<subsystem>/x.h, x.cpp    app, config, service, loggers, plugin_loader
manannan/test/                     one binary manannan_tests; fixtures in test/fixtures/
manannan/fuzz/                     route53_response_fuzz.cpp, checkip_body_fuzz.cpp
plugins/amazon_check_ip/, plugins/aws_route53/   unchanged place; they are separate .so targets
tools/ci.sh, tools/build-deb.sh
```

The exact subsystem split is settled in the layout step, from basemind's map,
and recorded here before the move. A move commit changes no content.

## Order

Each step is its own feature branch squashed onto develop, gated on a claimed
Pi (release build and the tests once; sanitizers only at the end of the
alignment, before 0.4.0). Moves, renames and content changes are separate
commits on the branch.

1. Identifying data. Replace the record names in tests/route53_test.cpp with
   example names (commit on main, before the rewrite). Then rewrite history
   with git filter-repo, replacing the real names in every blob. Re-point the
   tags (v0.2.0, v0.3.0 kept on the rewritten commits; add bare 0.2.0 and
   0.3.0). Delete the stale dependabot branch. Force-push main and all tags on
   the owner's word at the time. Record the old-to-new hash map in
   COMPLETED.md (the published 0.3.0 was built from 4ba2bc0). Known limit:
   GitHub keeps pull request refs (refs/pull/1-4/head) and their objects;
   only GitHub support can purge those, the owner's call.
2. develop from main; CLAUDE.md's branch rule updated.
3. Layout move (git mv only), then CMake paths fixed in a second commit.
4. `_private` to `_name`, and CLAUDE.md's naming rule with it.
5. Header block on every source, CMakeLists.txt and script; CMake 3.24;
   MANANNAN_BUILD_FUZZING; `-Wthread-safety` under clang.
6. CI: scripts/ to tools/ (move commit), ci.sh steps, workflows, codecov.yml,
   badges. Workflows are merged locally and pushed over ssh (the gh token
   lacks workflow scope).
7. Injected clock and wait in the service loop; service tests step time.
8. Secret type for the AWS credentials; error messages carry names, never
   values.
9. Configuration: config.yaml and secrets.yaml, overrides, log settings.
   Packaging: /usr/bin, system user, migration in postinst from
   manannan.yaml and the environment file. Docs updated in the same step.
10. Fuzz targets for the Route 53 response and checkip body.
11. Back-off with a budget and a parked state (serves self-healing
    disciplines; uses the clock from step 7).
12. CONTRIBUTING.md, CODE_OF_CONDUCT.md, CHANGELOG.md, docs/licensing.md
    (dependencies confirmed from CMake at the time).
13. Release 0.4.0: full tests once on a Pi (release, ASan+UBSan, TSan on the
    clang 19 Pi, the fuzz targets), CI green on develop, merge to main, tag
    0.4.0, build and stage. Node upgrade, with the migration checked at debug
    on each node, on the owner's go.
