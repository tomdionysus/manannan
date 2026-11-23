# Active

Open work, in the order it should be done. An item leaves this file when it
is in the tree and verified; its evidence then goes in COMPLETED.md.

## Resume here

Nothing is in progress. 0.3.0 is released in the apt repository and both
production nodes run it from the package. The tree is clean and pushed; CI,
CodeQL and Codecov are green on main. The next step is whichever item below
Tom picks.

## State of the tree

- Version 0.3.0, tag v0.3.0 (moved once, docs only, before anyone fetched it
  for a build; the published packages were built from 4ba2bc0 and are
  unaffected). Branch main only; remote github.com/tomdionysus/manannan.
- 59 GoogleTest tests; line coverage above 99% (Codecov shows 100% of the
  lines it counts). CI: format, linux (scripts/ci.sh: -Werror, ASan+UBSan,
  coverage to Codecov), clang, package (arm64, lintian clean), macos.
  CodeQL weekly and on push; Dependabot monthly for actions.
- Packages manannan, manannan-dev, manannan-dbgsym (debhelper, native),
  service installed disabled, hardened unit (systemd-analyze 1.1 OK).
- Both nodes on 0.3.0 since 2026-11-12/13, no restarts. Neither has written a
  record yet under the hardened unit, as the public IPs have not changed; the
  write path is covered by the tests.

## Decisions

- Packages only for Debian trixie arm64; other platforms build from source.
- The configuration is YAML through yaml-cpp; secrets only in the environment
  file, never in manannan.yaml.
- The service ships disabled; the packaged configuration is an example.
- Tags carry a `v` (scripts/build-deb.sh matches `v*`). The 2026 style
  document says bare `x.y.z`; changing it waits on Tom (below).
- README is install-and-set-up only; everything else lives in docs/.

## Waits on Tom

1. Style-document alignment, each its own change if wanted:
   a. tags `x.y.z` without `v` from the next release (build-deb.sh's match and
      docs/packaging.md change with it);
   b. a `develop` branch, with main reached only by merging it;
   c. the GPLv3 header block on every source file (only the logger files and
      the scripts and workflows carry one, in two different forms);
   d. a CONTRIBUTING.md as the public statement of the rules.
2. Observe the first real record update on either node (the next public IP
   change) and confirm the `set` line in the journal.

## Next

1. Fuzz targets for the parsers of untrusted bytes: the Route 53 response
   (xml_unescape, decode_dns_name, the record-set regexes) and the checkip
   body. Style rule: every reader of outside bytes has one.
2. AAAA records: the source is IPv4 only (checkip.amazonaws.com) and the
   registry writes A records only. Needs an IPv6 source and a record type
   setting.
3. Back-off on repeated failures: today a failing record is retried every
   cadence with no budget or parked state (see the self-healing disciplines).
   Low stakes at a 5-minute cadence, but the rule applies.
