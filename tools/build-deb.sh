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
#   tools/build-deb.sh [dpkg-buildpackage options]
#
# The version comes from `git describe`: the tag 0.4.0 gives 0.4.0, and a
# build three commits later 0.4.0+git3.gabc1234, which sorts after 0.4.0 and
# before 0.4.1. Where no tag is reachable it is the tree's version and
# +git.g<hash>. Uncommitted changes add .dirty. DEB_BUILD_OPTIONS=nocheck
# skips the tests.

set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"

project=$(sed -nE 's/^project\(Manannan VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)
if describe=$(git describe --tags --match '[0-9]*.[0-9]*.[0-9]*' --dirty 2>/dev/null); then
    version=$(echo "$describe" | sed -E 's/-([0-9]+)-g([0-9a-f]+)/+git\1.g\2/; s/-dirty$/.dirty/')
else
    describe=$(git describe --always --dirty)
    version=$(echo "$project+git.g$describe" | sed -E 's/-dirty$/.dirty/')
fi

# A build of a tag is a release, and has to be the version the tree says.
# Between tags the tree may already carry the next one.
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
