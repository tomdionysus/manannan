# Building from source

On Debian trixie and Raspberry Pi OS (trixie, 64-bit) on arm64, install the
package instead (see the [README](../README.md#install)). Build from source on
other distributions and architectures, or to work on Manannan.

## Requirements

- Linux with systemd. The package lists below cover Debian 12+, Ubuntu 24.04+,
  Raspberry Pi OS, Fedora, RHEL 9+ and its rebuilds, Arch and openSUSE
  Tumbleweed.
- A C++20 compiler, CMake 3.24+, yaml-cpp 0.6+, libcurl and OpenSSL libcrypto;
  GoogleTest 1.12+ for the tests.

```sh
# Debian, Ubuntu, Raspberry Pi OS
sudo apt install build-essential cmake git libyaml-cpp-dev libcurl4-openssl-dev libssl-dev libgtest-dev

# Fedora
sudo dnf install gcc-c++ make cmake git yaml-cpp-devel libcurl-devel openssl-devel gtest-devel

# Rocky Linux, AlmaLinux (yaml-cpp comes from EPEL)
sudo dnf install epel-release && sudo crb enable
sudo dnf install gcc-c++ make cmake git yaml-cpp-devel libcurl-devel openssl-devel gtest-devel

# Arch Linux
sudo pacman -S --needed base-devel cmake git yaml-cpp curl openssl gtest

# openSUSE Tumbleweed
sudo zypper install gcc-c++ make cmake git yaml-cpp-devel libcurl-devel libopenssl-devel gtest
```

On RHEL itself, enable CodeReady Builder with `subscription-manager` and
install the `epel-release` RPM from the Fedora project instead of the first
EL line.

## Build and install

```sh
git clone https://github.com/tomdionysus/manannan.git
cd manannan
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMANANNAN_BUILD_TESTING=ON
cmake --build build
./build/manannan_tests
sudo cmake --install build
```

The tests need no network: every request they make goes to a server of their
own on the loopback interface. Leave out `-DMANANNAN_BUILD_TESTING=ON` to build
without GoogleTest.

The default prefix is `/usr/local`; set `-DCMAKE_INSTALL_PREFIX` to change it.
The installed configuration and unit file are generated with the actual install
paths, so they stay consistent under any prefix or libdir layout.

| Installed | Path (default prefix) |
| --- | --- |
| Daemon | `/usr/local/bin/manannan` |
| Manual page | `/usr/local/share/man/man8/manannan.8` |
| Plugins | `/usr/local/lib/manannan/plugins` (`lib64` on RPM-based distributions) |
| Configuration | `/usr/local/etc/manannan/config.yaml` |
| Secrets (you create them) | `/usr/local/etc/manannan/secrets.yaml` |
| systemd unit | `/usr/local/lib/systemd/system/manannan.service` |
| System user | `/usr/local/lib/sysusers.d/manannan.conf` |
| Plugin headers | `/usr/local/include/manannan` |

Reinstalling never overwrites an existing `config.yaml`.

Configure as in [configuration.md](configuration.md), using these paths.
Create the service's user, and the secrets file readable by it alone:

```sh
sudo systemd-sysusers /usr/local/lib/sysusers.d/manannan.conf
sudo install -m 600 -o manannan -g manannan /dev/null /usr/local/etc/manannan/secrets.yaml
sudoedit /usr/local/etc/manannan/secrets.yaml
```

Then start the service:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now manannan
```

To upgrade, `git pull`, rebuild, run the tests and reinstall as above, then
`sudo systemctl restart manannan`.

## Uninstalling a source install

```sh
sudo systemctl disable --now manannan
sudo rm /usr/local/bin/manannan /usr/local/lib/systemd/system/manannan.service /usr/local/share/man/man8/manannan.8
sudo rm /usr/local/lib/sysusers.d/manannan.conf
sudo rm -rf /usr/local/lib*/manannan /usr/local/include/manannan /usr/local/etc/manannan
sudo systemctl daemon-reload
```

The last `rm` also removes the configuration and the secrets.

## Moving a source install to the package

The unit under `/usr/local/lib/systemd/system` overrides the package's, so the
source install has to go first.

1. Keep a copy of the configuration and secrets:

   ```sh
   sudo cp -a /usr/local/etc/manannan /root/manannan-backup
   ```

2. Uninstall the source install as above, then install the package (see the
   [README](../README.md#install)).
3. Copy the old configuration into `/etc/manannan/config.yaml`, changing
   `plugin_directories` to the packaged path (the one in the packaged file,
   such as `/usr/lib/aarch64-linux-gnu/manannan/plugins`). With the old path
   Manannan finds no plugins and does not start.
4. Copy the secrets over the package's, keeping their owner and mode:

   ```sh
   sudo install -m 600 -o manannan -g manannan /root/manannan-backup/secrets.yaml /etc/manannan/secrets.yaml
   ```

5. `sudo systemctl enable --now manannan` and check `journalctl -u manannan`.

## Development

| Option | Default | |
| --- | --- | --- |
| `MANANNAN_BUILD_TESTING` | `OFF` | Build `manannan_tests` (GoogleTest) |
| `MANANNAN_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors |
| `MANANNAN_BUILD_FUZZING` | `OFF` | Build each fuzz target (clang and libFuzzer) |

`tools/ci.sh <step>` runs one CI step on Debian trixie, the same in GitHub's
runner as on a build machine:

| Step | What it does |
| --- | --- |
| `check` | the header block on every source; clang-format |
| `release` | g++ release build, warnings as errors, the tests |
| `clang` | the same with clang++ and its thread-safety analysis |
| `tidy` | clang-tidy's use-after-move check |
| `asan` | the tests under AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | the tests under ThreadSanitizer |
| `package` | install, then build and run a plugin against the installed headers |
| `coverage` | the tests in a coverage build, with an lcov report |
| `fuzz` | each fuzz target for `FUZZ_SECONDS` (default 60) |

Builds go under `build-ci/`. `tools/build-deb.sh` builds the Debian packages;
see [packaging.md](packaging.md).

`.clang-format` is authoritative: run `clang-format -i` on the files you
change. CI fails any source that clang-format would change.

The project also builds on macOS (`brew install cmake yaml-cpp googletest
openssl`) for development; there is no service integration there.
[PLUGINS.md](PLUGINS.md) describes the plugin contract.
