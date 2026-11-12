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
scripts/build-deb.sh
```

The packages are written to `build-deb/`. The tests run inside the build;
`DEB_BUILD_OPTIONS=nocheck` skips them. CI builds the packages on arm64 and
fails on any lintian error or warning.

## Versions

The version comes from `git describe`. The tag `v0.3.0` gives `0.3.0`; a build
three commits later gives `0.3.0+git3.gabc1234`, which sorts after `0.3.0` and
before `0.3.1`; uncommitted changes add `.dirty`. A build of a tag must match
`project(Manannan VERSION ...)` in `CMakeLists.txt`, or the script refuses it.
Only builds of a tag are released.

## What the packages do

- The configuration is a conffile, so an upgrade keeps local changes and asks
  before replacing a changed file.
- The postinst creates `/etc/manannan/environment`, mode 0600, if it does not
  exist. It holds the keys and is never part of the package.
- The service is installed disabled and not started, since the packaged
  configuration is an example. An upgrade restarts it if it is enabled or
  running.
- `apt remove` keeps `/etc/manannan`; `apt purge` deletes it.
