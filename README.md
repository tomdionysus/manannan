# Manannan

[![CI](https://github.com/tomdionysus/manannan/actions/workflows/ci.yml/badge.svg)](https://github.com/tomdionysus/manannan/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/tomdionysus/manannan/graph/badge.svg)](https://codecov.io/gh/tomdionysus/manannan)
[![CodeQL](https://github.com/tomdionysus/manannan/actions/workflows/codeql.yml/badge.svg)](https://github.com/tomdionysus/manannan/actions/workflows/codeql.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

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

## Install

Packages are published for Debian trixie and Raspberry Pi OS (trixie, 64-bit)
on arm64. Add the package repository and install:

```sh
sudo curl -fsSLo /etc/apt/sources.list.d/tomcully.sources https://tomcully.com/apt/tomcully.sources
sudo apt update && sudo apt install manannan
```

The service is installed but not enabled, because the packaged configuration
is an example. Set it up in three steps:

1. Put your records in `/etc/manannan/manannan.yaml` (see
   [Configuration](#configuration)).
2. Put your AWS keys in `/etc/manannan/environment` (see
   [Keys](#keys)).
3. Start the service:

   ```sh
   sudo systemctl enable --now manannan
   journalctl -u manannan -f
   ```

On other distributions and architectures, build from source: see
[docs/building.md](docs/building.md).

| Installed | Path |
| --- | --- |
| Daemon | `/usr/sbin/manannan` |
| Configuration | `/etc/manannan/manannan.yaml` |
| Keys | `/etc/manannan/environment` (root only) |
| Plugins | `/usr/lib/<triplet>/manannan/plugins`, for example `/usr/lib/aarch64-linux-gnu/manannan/plugins` |
| systemd unit | `/usr/lib/systemd/system/manannan.service` |
| Manual page | `man 8 manannan` |

## Configuration

`/etc/manannan/manannan.yaml`:

```yaml
manannan:
  cadence: 5m
  log_level: info
  source: amazon_check_ip
  registries:
    - aws_route53
  plugin_directories:
    - /usr/lib/aarch64-linux-gnu/manannan/plugins

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

Usually only the `records` list needs editing: the packaged file already has
the right `plugin_directories` for the machine. The `manannan` block selects
plugins, and each plugin reads its own block, at `source.<name>` or
`registry.<name>`. A value written as `${NAME}` is taken from the environment.

The configuration is a conffile: upgrades keep your changes.

### Keys

Secrets belong in `/etc/manannan/environment`, never in `manannan.yaml`, which
is world-readable. The package creates the file empty and root-only; edit it
with:

```sh
sudoedit /etc/manannan/environment
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

After changing the configuration or the keys, restart the service:

```sh
sudo systemctl restart manannan
journalctl -u manannan -f
```

At `info`, Manannan logs plugin loads, the records it manages and each change
it makes. Records that are already current are logged only at `debug`:

```
[INFO ] loaded plugin amazon_check_ip
[INFO ] loaded plugin aws_route53
[INFO ] managing aws_route53:home.example.com
[INFO ] Manannan 0.3.0 started
[INFO ] set aws_route53:home.example.com to 203.0.113.7
```

Configuration errors stop startup with a message naming the offending setting:
unknown keys, duplicate records, missing zones and incomplete credentials are
all rejected. Runtime failures, such as a provider returning an error, are
logged against the record and retried on the next cycle.

For a full trace, add `LOG_LEVEL=debug` to `/etc/manannan/environment` and
restart. The variable overrides `log_level` in the configuration.

The service starts at boot once the network is online, and runs as an
unprivileged dynamic user with no capabilities and a restricted set of
system calls.

## Upgrading

```sh
sudo apt update && sudo apt upgrade
```

An upgrade restarts the service if it is running, and keeps your
configuration and keys.

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
| `endpoint` | `route53.amazonaws.com` | Host name, optionally with a port and an `http://` or `https://` scheme (default `https`) |
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
sudo apt remove manannan   # keeps /etc/manannan
sudo apt purge manannan    # also deletes /etc/manannan, keys included
```

## More

- [docs/building.md](docs/building.md): building from source, installing
  under `/usr/local`, moving a source install to the package, and
  development.
- [docs/PLUGINS.md](docs/PLUGINS.md): writing source and registry plugins
  (headers in `manannan-dev`).
- [docs/packaging.md](docs/packaging.md): how the Debian packages are built
  and versioned.
- [docs/NAME.md](docs/NAME.md): the name.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE).
