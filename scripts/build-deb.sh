#!/bin/sh
#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
# Builds the Debian packages (manannan, manannan-dev and debug symbols) on
# Debian trixie from a git checkout, natively for the machine's architecture.
# The packages are written to build-deb/.
#
#   sudo apt install dpkg-dev && sudo apt build-dep ./
#   scripts/build-deb.sh [dpkg-buildpackage options]
#
# The version comes from `git describe`: the tag v0.3.0 gives 0.3.0, and a
# build three commits later 0.3.0+git3.gabc1234, which sorts after 0.3.0 and
# before 0.3.1. Uncommitted changes add .dirty. DEB_BUILD_OPTIONS=nocheck
# skips the tests.

set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"

describe=$(git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' --dirty)
version=$(echo "${describe#v}" | sed -E 's/-([0-9]+)-g([0-9a-f]+)/+git\1.g\2/; s/-dirty$/.dirty/')

# A build of a tag is a release, and has to be the version the tree says.
# Between tags the tree may already carry the next one.
project=$(sed -nE 's/^project\(Manannan VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)
case "$version" in
    *+git*) ;;
    "$project" | "$project".dirty) ;;
    *)
        echo "build-deb.sh: the tag says $describe and CMakeLists.txt says $project" >&2
        exit 1
        ;;
esac

cat >debian/changelog <<END
manannan ($version) trixie; urgency=medium

  * Built from $describe.

 -- tomdionysus <tomdionysus@users.noreply.github.com>  $(date -R)
END

dpkg-buildpackage -us -uc -b "$@"

mkdir -p build-deb
for f in ../manannan*_"$version"_*.deb ../manannan_"$version"_*.buildinfo ../manannan_"$version"_*.changes; do
    if [ -e "$f" ]; then mv "$f" build-deb/; fi
done
ls -l build-deb/*"$version"*
