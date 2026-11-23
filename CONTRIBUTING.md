# Contributing to Manannan

## Read first

`TODO/ACTIVE.md` is the plan and `TODO/COMPLETED.md` is the record. The
self-healing disciplines govern the service loop: every retry has back-off, a
budget, a visible parked state and an operator action, and nothing leaves a
node in a state it cannot recover from on its own. A change that touches
scheduling or retry says which discipline it follows.

## Branches, tags and releases

- `main` is the last stable state, reached only by merging `develop` at a
  release. `develop` is where work integrates; work is built on
  `feature/<short-name>` and lands on `develop` as one squash commit.
- Tags are exactly `x.y.z`: annotated, no `v`, cut from `main`. Releases
  before 0.4.0 also carry the older `vx.y.z` tags.
- The tag, `project(Manannan VERSION ...)`, `manannan --version` and the
  Debian package version are one number.
- The version commit adds the release to `CHANGELOG.md`, newest first: what
  changed for people running Manannan or writing plugins.
- A change to the plugin interface (`manannan/include/manannan/plugin.h` and
  the logger it hands plugins) bumps `plugin_abi_version`.

## Commits

- Subjects are short imperative prose. No emoji, no ticket prefixes.
- Keep unrelated changes in separate commits: a move, a rename, a fix and a
  feature are four commits. A commit that moves files does not change their
  content.
- A commit message ends with its last line of prose. No tool attribution
  trailers.

## Building and testing

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMANANNAN_BUILD_TESTING=ON -DMANANNAN_WARNINGS_AS_ERRORS=ON
cmake --build build -j4
./build/manannan_tests
```

Run the test binary rather than `ctest`. The tests need no network: check IP
and Route 53 are a server of their own on the loopback interface, SigV4 is
checked against vectors computed independently, and the service loop runs on
a clock the tests step, so nothing sleeps.

`tools/ci.sh <step>` runs one CI step the same on a build machine as in
GitHub's runner (see `docs/building.md`). Hosted CI runs every step on each
push to `develop` and `main`: the header and format check; g++ and clang
release builds; clang-tidy; ASan with UBSan and TSan; the fuzz targets; a
plugin built against the installed headers; coverage to Codecov; the Debian
packages on x86-64 and arm64, linted; and a build and test on macOS. Locally,
sanitizers run at the end of a feature, before a release, or to hunt a bug.

Write tests from what is required, watch them fail, then write the code.
Coverage is held near 100% of lines; `LCOV_EXCL_LINE` marks only lines a test
cannot reach.

## What CI cannot run

Run by hand before a release:

- An upgrade of the package from the previous release on a Debian trixie
  arm64 machine, with that release's configuration and keys in place, through
  to a running service (the migration, the user, the file modes).
- A check, after the release reaches a node, that its first cycle logs no
  errors and that a changed address is written.

## Dependencies

yaml-cpp (the configuration), libcurl (HTTPS), OpenSSL's libcrypto (SHA-256,
HMAC, cleansing), and GoogleTest for the tests; their licences are in
`docs/licensing.md`. Prefer writing something by hand over adding a library,
and say why when a dependency really is the right call.

## Layout

```
manannan/include/manannan/   the plugin interface: plugin.h, loggers/logger.h,
                             version.h.in; all that manannan-dev installs
manannan/daemon/             the daemon: configuration, plugin loader, clock,
                             service loop, migration, main (manannan::daemon)
manannan/loggers/            the loggers (manannan::loggers)
manannan/test/               GoogleTest, <subsystem>_<thing>_test.cpp, one
                             binary; fixtures in fixtures/, test plugins in
                             plugins/, the package consumer in package/
manannan/fuzz/               a libFuzzer target per reader of outside bytes
plugins/                     the bundled plugins, each its own module
tools/                       ci.sh and build-deb.sh
debian/, packaging/, man/,   packaging, the unit and sysusers file, the
config/                      manual page and the configuration template
docs/                        how it works, configuration, running, building,
                             packaging, plugins
```

## Style

- `.clang-format` (Google base, 160 columns) is authoritative: run
  `clang-format -i` on the files you change. `#pragma once`, headers `.h`.
- `snake_case` for functions, variables and files; `PascalCase` for types;
  private members `_name`; one namespace per directory.
- Every source, CMake file and script opens with the project's header block.
- Components take their collaborators in their constructors; `application.cpp`
  is the composition root.
- Every reader of bytes from outside the process is bounded, refuses what it
  does not understand, and has a fuzz target. Secrets are `Secret`s, never
  logged.
- Comments are sparse and explain intent. No emoji anywhere.
- Docs state current facts and name no hosts, addresses, people or dates.
