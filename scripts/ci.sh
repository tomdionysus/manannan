#!/bin/sh
#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
# CI for the daemon and plugins: the tests in a release build with warnings as
# errors, under AddressSanitizer and UndefinedBehaviorSanitizer, and in a
# coverage build whose lcov report is build-coverage/coverage.info. Runs on
# Debian trixie, in GitHub's runner or on a Pi.
#
#   scripts/ci.sh
#
# Needs: build-essential cmake libyaml-cpp-dev libcurl4-openssl-dev libssl-dev
# libgtest-dev lcov.

set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"
jobs=$(nproc 2>/dev/null || echo 4)

step() { printf '\n== %s\n' "$1"; }

step "release build and tests"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMANANNAN_BUILD_TESTING=ON -DMANANNAN_WARNINGS_AS_ERRORS=ON >/dev/null
cmake --build build-release -j "$jobs"
build-release/tests/manannan_tests

step "AddressSanitizer and UndefinedBehaviorSanitizer"
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMANANNAN_BUILD_TESTING=ON -DMANANNAN_WARNINGS_AS_ERRORS=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined" >/dev/null
cmake --build build-asan -j "$jobs"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    build-asan/tests/manannan_tests

step "coverage"
cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DMANANNAN_BUILD_TESTING=ON -DMANANNAN_COVERAGE=ON >/dev/null
find build-coverage -name '*.gcda' -delete
cmake --build build-coverage -j "$jobs"
build-coverage/tests/manannan_tests
ignore=inconsistent,inconsistent,mismatch,negative,unused
lcov --quiet --capture --directory build-coverage --ignore-errors "$ignore" --output-file build-coverage/all.info
lcov --quiet --extract build-coverage/all.info "$top/src/*" "$top/include/*" "$top/plugins/*" \
    --ignore-errors "$ignore" --output-file build-coverage/coverage.info
rm build-coverage/all.info
lcov --summary build-coverage/coverage.info --ignore-errors "$ignore"

step "passed"
