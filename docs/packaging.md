# Packaging

Manannan is packaged for Debian trixie as `manannan` (the daemon, the bundled
plugins, the configuration, the systemd unit and the manual page),
`manannan-dev` (the plugin headers) and `manannan-dbgsym`. The packages are
built with debhelper from `debian/`, driving the CMake build; the CMake install
rules decide where every file goes.

## Building

On Debian trixie, natively for the machine's architecture, from a git
checkout:

```sh
sudo apt install dpkg-dev
sudo apt build-dep ./
tools/build-deb.sh
```

The packages are written to `build-deb/`. The tests run inside the build;
`DEB_BUILD_OPTIONS=nocheck` skips them. CI builds the packages on x86-64 and
arm64 and fails on any lintian error or warning.

## Versions

The version comes from `git describe`. The tag `0.4.0` gives `0.4.0`; a build
three commits later gives `0.4.0+git3.gabc1234`, which sorts after `0.4.0` and
before `0.4.1`; where no tag is reachable it is the tree's version and
`+git.g<hash>`; uncommitted changes add `.dirty`. A build of a tag must match
`project(Manannan VERSION ...)` in `CMakeLists.txt`, or the script refuses it.
Only builds of a tag are released. Tags are `x.y.z`, annotated, on `main`;
releases before 0.4.0 also carry the older `vx.y.z` tags.

## Releasing

1. On `develop`, set the new version in `project(Manannan VERSION ...)` and
   commit. Merge `develop` to `main` and tag `x.y.z` (annotated) on `main`.
2. Build from a clean clone of the tag on Debian trixie arm64 with
   `tools/build-deb.sh`, and check it with
   `lintian --fail-on error,warning build-deb/*.changes`.
3. Add the packages to the apt repository, which signs them, and upload it.

A version, once published, is never rebuilt: any change, however small, is a
new version.

## What the packages do

- The configuration is a conffile, so an upgrade keeps local changes and asks
  before replacing a changed file.
- The postinst creates `/etc/manannan/environment`, mode 0600, if it does not
  exist. It holds the keys and is never part of the package.
- The service is installed disabled and not started, since the packaged
  configuration is an example. An upgrade restarts it if it is enabled or
  running.
- `apt remove` keeps `/etc/manannan`; `apt purge` deletes it.
