# Configuration

Manannan reads `/etc/manannan/manannan.yaml` (`/usr/local/etc/manannan` for a
source install) and takes its secrets from the environment file beside it.
Restart the service after changing either.

## The configuration file

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

The `manannan` block selects plugins, and each plugin reads its own block, at
`source.<name>` or `registry.<name>`. A value written as `${NAME}` is taken
from the environment.

The packaged file already has the right `plugin_directories` for the machine;
usually only `records` needs editing. It is a conffile, so upgrades keep your
changes.

## Keys

Secrets belong in `/etc/manannan/environment`, never in `manannan.yaml`, which
is world-readable. The package creates the file empty and root-only:

```sh
sudoedit /etc/manannan/environment
```

```
AWS_ACCESS_KEY_ID=AKIA...
AWS_SECRET_ACCESS_KEY=...
OFFICE_AWS_ACCESS_KEY_ID=AKIA...
OFFICE_AWS_SECRET_ACCESS_KEY=...
```

## Route 53 credentials

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

## Settings

### `manannan`

| Setting | Default | |
| --- | --- | --- |
| `cadence` | required | Interval: integer plus `ms`, `s`, `m` or `h` |
| `log_level` | `info` | `debug`, `info`, `warn`, `error`; `LOG_LEVEL` in the environment overrides it |
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
