#!/bin/sh
#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
# One CI step on Debian trixie, in GitHub's runner or on a build machine;
# .github/workflows/ci.yml runs each as its own job.
#
#   tools/ci.sh check      the header block on every source, and clang-format
#   tools/ci.sh release    g++ release build, warnings as errors, and the tests
#   tools/ci.sh clang      the same with clang++ (and -Wthread-safety)
#   tools/ci.sh tidy       clang-tidy bugprone-use-after-move over every source
#   tools/ci.sh asan       AddressSanitizer and UndefinedBehaviorSanitizer
#   tools/ci.sh tsan       ThreadSanitizer
#   tools/ci.sh package    install, then build and run a plugin-author's consumer
#                          of the installed headers (manannan/test/package)
#   tools/ci.sh coverage   a coverage build's tests; lcov report build-ci/coverage.info
#   tools/ci.sh fuzz       each fuzz target for FUZZ_SECONDS (default 60), with
#                          ASan and UBSan, under clang's libFuzzer
#
# Builds go under build-ci/ (CI_OUT to move it); CI_JOBS sets the parallel
# jobs (default: every core). Needs g++ cmake libgtest-dev libyaml-cpp-dev
# libcurl4-openssl-dev libssl-dev, ninja-build if present, and for its step
# clang, clang-tidy, clang-format, libclang-rt-<version>-dev or lcov.

PROJECT=manannan
OPTION_PREFIX=MANANNAN
HEADER_LINE='// Manannan - plugin-based dynamic DNS updater'

set -eu

top=$(cd "$(dirname "$0")/.." && pwd)
out=${CI_OUT:-$top/build-ci}
jobs=${CI_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}
step=${1:-}
tests=${PROJECT}_tests
generator=
if command -v ninja >/dev/null 2>&1; then generator="-G Ninja"; fi

# Warnings are errors in every build but the sanitizers': instrumented, g++ warns
# inside the standard library. The release and clang steps hold the warning gate.
# The plugins are modules, so every linker gets the flags.
configure() {
  dir="$1"
  shift
  # shellcheck disable=SC2086
  cmake -S "$top" -B "$out/$dir" $generator -D${OPTION_PREFIX}_BUILD_TESTING=ON -D${OPTION_PREFIX}_WARNINGS_AS_ERRORS=ON "$@"
  cmake --build "$out/$dir" -j "$jobs" --target "$tests"
}

case "$step" in
  check)
    cd "$top"
    missing=0
    for f in $(git ls-files '*.h' '*.cpp' '*.h.in'); do
      if ! head -3 "$f" | grep -qxF "$HEADER_LINE"; then
        echo "no header block: $f"
        missing=1
      fi
    done
    for f in $(git ls-files 'CMakeLists.txt' '*/CMakeLists.txt' '*.sh' '*.yml'); do
      if ! head -4 "$f" | grep -qxF "# ${HEADER_LINE#// }"; then
        echo "no header block: $f"
        missing=1
      fi
    done
    [ "$missing" = 0 ]
    git ls-files '*.h' '*.cpp' | xargs clang-format --dry-run -Werror
    ;;
  release)
    configure release -DCMAKE_BUILD_TYPE=Release
    "$out/release/$tests"
    ;;
  clang)
    CXX=${CLANGXX:-clang++} configure clang -DCMAKE_BUILD_TYPE=Release
    "$out/clang/$tests"
    ;;
  tidy)
    # shellcheck disable=SC2086
    CXX=${CLANGXX:-clang++} cmake -S "$top" -B "$out/tidy" $generator -D${OPTION_PREFIX}_BUILD_TESTING=ON -D${OPTION_PREFIX}_BUILD_FUZZING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cd "$top"
    git ls-files '*.cpp' | xargs clang-tidy -p "$out/tidy" --quiet --checks='-*,bugprone-use-after-move' --warnings-as-errors='*'
    ;;
  asan)
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
    configure asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -D${OPTION_PREFIX}_WARNINGS_AS_ERRORS=OFF -DCMAKE_CXX_FLAGS="$flags" \
      -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags" -DCMAKE_MODULE_LINKER_FLAGS="$flags"
    ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1 UBSAN_OPTIONS=print_stacktrace=1 "$out/asan/$tests"
    ;;
  tsan)
    flags="-fsanitize=thread -fno-omit-frame-pointer"
    configure tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -D${OPTION_PREFIX}_WARNINGS_AS_ERRORS=OFF -DCMAKE_CXX_FLAGS="$flags" \
      -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags" -DCMAKE_MODULE_LINKER_FLAGS="$flags"
    TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" "$out/tsan/$tests"
    ;;
  package)
    version=$(sed -n "s/^project(Manannan VERSION \([0-9.]*\).*/\1/p" "$top/CMakeLists.txt")
    rm -rf "$out/package"
    # shellcheck disable=SC2086
    cmake -S "$top" -B "$out/package/build" $generator -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$out/package/prefix"
    cmake --build "$out/package/build" -j "$jobs"
    cmake --install "$out/package/build"
    # shellcheck disable=SC2086
    cmake -S "$top/$PROJECT/test/package" -B "$out/package/consumer" $generator -DCMAKE_PREFIX_PATH="$out/package/prefix"
    cmake --build "$out/package/consumer"
    "$out/package/consumer/consumer" "$version"
    ;;
  coverage)
    flags=--coverage
    configure coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" \
      -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags" -DCMAKE_MODULE_LINKER_FLAGS="$flags"
    find "$out/coverage" -name '*.gcda' -delete
    (cd "$out/coverage" && "./$tests")
    ignore=inconsistent,inconsistent,mismatch,negative,unused
    lcov --quiet --capture --directory "$out/coverage" --ignore-errors "$ignore" --output-file "$out/all.info"
    lcov --quiet --extract "$out/all.info" "$top/$PROJECT/*" "$top/plugins/*" --ignore-errors "$ignore" --output-file "$out/project.info"
    lcov --quiet --remove "$out/project.info" "$top/$PROJECT/test/*" "$top/$PROJECT/fuzz/*" --ignore-errors "$ignore" --output-file "$out/coverage.info"
    rm "$out/all.info" "$out/project.info"
    lcov --summary "$out/coverage.info" --ignore-errors "$ignore"
    ;;
  fuzz)
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
    # shellcheck disable=SC2086
    CXX=${CLANGXX:-clang++} cmake -S "$top" -B "$out/fuzz" $generator -DCMAKE_BUILD_TYPE=RelWithDebInfo -D${OPTION_PREFIX}_BUILD_FUZZING=ON \
      -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags" -DCMAKE_MODULE_LINKER_FLAGS="$flags"
    cmake --build "$out/fuzz" -j "$jobs"
    for target in "$out"/fuzz/*_fuzz; do
      corpus="$out/fuzz/corpus/$(basename "$target")"
      mkdir -p "$corpus"
      "$target" -max_total_time="${FUZZ_SECONDS:-60}" -print_final_stats=1 "$corpus"
    done
    ;;
  *)
    echo "usage: tools/ci.sh check|release|clang|tidy|asan|tsan|package|coverage|fuzz" >&2
    exit 2
    ;;
esac
