# Configuration

Manannan reads `/etc/manannan/config.yaml` and the secrets in
`/etc/manannan/secrets.yaml` beside it (`/usr/local/etc/manannan` for a source
install). Restart the service after changing either.

## The configuration file

```yaml
manannan:
  cadence: 5m
  source: amazon_check_ip
  registries:
    - aws_route53
  plugin_directories:
    - /usr/lib/aarch64-linux-gnu/manannan/plugins

log:
  level: info
  format: text

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
```

The `manannan` block selects plugins, and each plugin reads its own block, at
`source.<name>` or `registry.<name>`. A value written as `${NAME}` is taken
from the environment.

The packaged file already has the right `plugin_directories` for the machine;
usually only `records` needs editing. It is a conffile, so upgrades keep your
changes. It holds no secrets and is readable by everyone.

## Secrets

Keys go in `/etc/manannan/secrets.yaml`, which the package creates owned by
the `manannan` user that the service runs as, and readable by it alone.
Manannan refuses to start if anyone else can read it.

```sh
sudoedit /etc/manannan/secrets.yaml
```

```yaml
registry:
  aws_route53:
    credentials:
      access_key_id: AKIA...
      secret_access_key: ...
```

The secrets file is merged over the configuration: mappings merge key by
key, and anything else, a list included, replaces what the configuration
has. So a record with credentials of its own is written in secrets.yaml by
repeating the whole `records` list there.

The daemon keeps the secret key and session token in memory that is locked
out of swap, left out of core dumps and wiped when it is released, and never
writes them to the log.

## Overriding a setting

Any setting the two files give a single value to, and `log.level` and
`log.format` always, can be overridden without editing them:

| Setting | Environment | Flag |
| --- | --- | --- |
| `log.level` | `MANANNAN_LOG_LEVEL` | `--log-level` |
| `manannan.cadence` | `MANANNAN_MANANNAN_CADENCE` | `--manannan-cadence` |
| `registry.aws_route53.ttl` | `MANANNAN_REGISTRY_AWS_ROUTE53_TTL` | `--registry-aws-route53-ttl` |

The environment variable is `MANANNAN_` and the path in capitals with `.`
as `_`; the flag is the path with `.` and `_` as `-`. A flag overrides the
environment, which overrides the files. An empty variable is ignored; an
unknown flag stops startup. `--config` and `--secrets` name the two files.

## Route 53 credentials

Credentials cascade. The plugin-level `credentials` block applies to every
record, and any field it leaves empty falls back to `AWS_ACCESS_KEY_ID`,
`AWS_SECRET_ACCESS_KEY` and `AWS_SESSION_TOKEN` in the environment. A
record's own `credentials` block replaces the base identity entirely and has
no environment fallback, so it must be complete.

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

## Settings

### `manannan`

| Setting | Default | |
| --- | --- | --- |
| `cadence` | required | Interval: integer plus `ms`, `s`, `m` or `h` |
| `source` | required | Source plugin name |
| `registries` | required | Registry plugin names |
| `plugin_directories` | required | Directories scanned for `.so` plugins |

### `log`

| Setting | Default | |
| --- | --- | --- |
| `level` | `info` | `all`, `debug`, `info`, `warn` or `error` |
| `format` | `text` | `text`, or `json` for one JSON object per line |

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
