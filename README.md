# Manannan

Manannan is a plugin-based dynamic DNS updater. It runs as a system service
and, on a fixed cadence, reads a value from a **source** plugin (typically the
host's public IP address) and reconciles it with every record held by one or
more **registry** plugins (typically DNS providers). Each record is checked and
updated independently, so a failure in one zone or account does not hold up the
others.

Bundled plugins:

| Plugin | Kind | Purpose |
| --- | --- | --- |
| `amazon_check_ip` | source | Public IPv4 address from `checkip.amazonaws.com` |
| `aws_route53` | registry | `A` records in AWS Route 53, across any number of hosted zones and AWS accounts |

Other sources and registries are separate shared libraries; see
[docs/PLUGINS.md](docs/PLUGINS.md).

## Requirements

- Linux with systemd. The package lists below cover Debian 12+, Ubuntu 22.04+,
  Raspberry Pi OS, Fedora, RHEL 9+ and its rebuilds, Arch and openSUSE
  Tumbleweed.
- A C++20 compiler, CMake 3.20+, yaml-cpp 0.6+, libcurl and OpenSSL libcrypto.

## Build dependencies

```sh
# Debian, Ubuntu, Raspberry Pi OS
sudo apt install build-essential cmake git libyaml-cpp-dev libcurl4-openssl-dev libssl-dev

# Fedora
sudo dnf install gcc-c++ make cmake git yaml-cpp-devel libcurl-devel openssl-devel

# Rocky Linux, AlmaLinux (yaml-cpp comes from EPEL)
sudo dnf install epel-release && sudo crb enable
sudo dnf install gcc-c++ make cmake git yaml-cpp-devel libcurl-devel openssl-devel

# Arch Linux
sudo pacman -S --needed base-devel cmake git yaml-cpp curl openssl

# openSUSE Tumbleweed
sudo zypper install gcc-c++ make cmake git yaml-cpp-devel libcurl-devel libopenssl-devel
```

On RHEL itself, enable CodeReady Builder with `subscription-manager` and
install the `epel-release` RPM from the Fedora project instead of the first
EL line.

## Build and install

```sh
git clone https://github.com/tomdionysus/mannanan.git
cd mannanan
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

The default prefix is `/usr/local`; set `-DCMAKE_INSTALL_PREFIX` to change it.
The installed configuration and unit file are generated with the actual install
paths, so they stay consistent under any prefix or libdir layout.

| Installed | Path (default prefix) |
| --- | --- |
| Daemon | `/usr/local/sbin/manannan` |
| Plugins | `/usr/local/lib/manannan/plugins` (`lib64` on RPM-based distributions) |
| Configuration | `/usr/local/etc/manannan/manannan.yaml` |
| Environment file (you create it) | `/usr/local/etc/manannan/environment` |
| systemd unit | `/usr/local/lib/systemd/system/manannan.service` |
| Plugin headers | `/usr/local/include/manannan` |

Reinstalling never overwrites an existing `manannan.yaml`.

## Configuration

```yaml
manannan:
  cadence: 5m
  log_level: info
  source: amazon_check_ip
  registries:
    - aws_route53
  plugin_directories:
    - /usr/local/lib/manannan/plugins

source:
  amazon_check_ip:
    timeout_seconds: 10

registry:
  aws_route53:
    ttl: 60
    records:
      - name: home.example.com
        hosted_zone_id: Z0123456789ABCDEFGHIJ
      - name: example.org
        hosted_zone_id: Z9876543210KLMNOPQRST
        ttl: 300
      - name: office.example.net
        hosted_zone_id: Z5555555555UVWXYZABCD
        credentials:
          access_key_id: ${OFFICE_AWS_ACCESS_KEY_ID}
          secret_access_key: ${OFFICE_AWS_SECRET_ACCESS_KEY}
```

The `manannan` block selects plugins. Each plugin reads its own block, at
`source.<name>` or `registry.<name>`. A value written as `${NAME}` is taken
from the environment.

Secrets belong in the environment file, which the unit loads via
`EnvironmentFile=`. It must stay root-only because `manannan.yaml` is
world-readable (the service runs under `DynamicUser=`):

```sh
sudo install -m 600 /dev/null /usr/local/etc/manannan/environment
sudoedit /usr/local/etc/manannan/environment
```

```
AWS_ACCESS_KEY_ID=AKIA...
AWS_SECRET_ACCESS_KEY=...
OFFICE_AWS_ACCESS_KEY_ID=AKIA...
OFFICE_AWS_SECRET_ACCESS_KEY=...
```

### Route 53 credentials

Credentials cascade. The plugin-level `credentials` block applies to every
record, and any field it leaves empty falls back to `AWS_ACCESS_KEY_ID`,
`AWS_SECRET_ACCESS_KEY` and `AWS_SESSION_TOKEN`, so with the environment file
above it can be omitted. A record's own `credentials` block replaces the base
identity entirely and has no environment fallback, so it must be complete.

Each identity needs only the following, scoped to the hosted zones it serves:

```json
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Action": ["route53:ListResourceRecordSets", "route53:ChangeResourceRecordSets"],
    "Resource": ["arn:aws:route53:::hostedzone/Z0123456789ABCDEFGHIJ"]
  }]
}
```

Attach it to a dedicated IAM user and issue that user an access key.

## Running

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now manannan
journalctl -u manannan -f
```

At `info`, Manannan logs plugin loads, the records it manages and each change
it makes. Records that are already current are logged only at `debug`:

```
[INFO ] loaded plugin amazon_check_ip
[INFO ] loaded plugin aws_route53
[INFO ] managing aws_route53:home.example.com
[INFO ] set aws_route53:home.example.com to 203.0.113.7
```

Configuration errors stop startup with a message naming the offending setting:
unknown keys, duplicate records, missing zones and incomplete credentials are
all rejected. Runtime failures, such as a provider returning an error, are
logged against the record and retried on the next cycle.

For a full trace, set `LOG_LEVEL=debug` in the environment file and restart.
The variable overrides `log_level` in the configuration.

To upgrade, `git pull`, rebuild, run the tests and reinstall as above, then
`sudo systemctl restart manannan`.

## Settings

### `manannan`

| Setting | Default | |
| --- | --- | --- |
| `cadence` | required | Interval: integer plus `ms`, `s`, `m` or `h` |
| `log_level` | `info` | `debug`, `info`, `warn`, `error` |
| `source` | required | Source plugin name |
| `registries` | required | Registry plugin names |
| `plugin_directories` | required | Directories scanned for `.so` plugins |

### `source.amazon_check_ip`

| Setting | Default | |
| --- | --- | --- |
| `endpoint` | `https://checkip.amazonaws.com/` | Must return a bare IPv4 address |
| `timeout_seconds` | `10` | |

### `registry.aws_route53`

| Setting | Default | |
| --- | --- | --- |
| `records` | required | List of records, below |
| `ttl` | `60` | Default TTL in seconds |
| `credentials` | `AWS_*` environment | `access_key_id`, `secret_access_key`, `session_token` |
| `timeout_seconds` | `10` | |
| `endpoint` | `route53.amazonaws.com` | |
| `signing_region` | `us-east-1` | SigV4 signing region |

Each record:

| Setting | Default | |
| --- | --- | --- |
| `name` | required | Record name; case and trailing dot are ignored |
| `hosted_zone_id` | required | With or without the `/hostedzone/` prefix |
| `ttl` | plugin `ttl` | |
| `credentials` | plugin credentials | Complete replacement, no environment fallback |

## Uninstalling

```sh
sudo systemctl disable --now manannan
sudo rm /usr/local/sbin/manannan /usr/local/lib/systemd/system/manannan.service
sudo rm -rf /usr/local/lib*/manannan /usr/local/include/manannan /usr/local/etc/manannan
sudo systemctl daemon-reload
```

The last `rm` also removes the configuration and the environment file.

## Development

The project also builds on macOS (`brew install cmake yaml-cpp curl openssl`)
for development; there is no service integration there.
[docs/PLUGINS.md](docs/PLUGINS.md) describes the plugin contract, and
[docs/NAME.md](docs/NAME.md) the name.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE).
