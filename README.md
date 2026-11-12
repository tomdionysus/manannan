# Manannan

[![CI](https://github.com/tomdionysus/manannan/actions/workflows/ci.yml/badge.svg)](https://github.com/tomdionysus/manannan/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/tomdionysus/manannan/graph/badge.svg)](https://codecov.io/gh/tomdionysus/manannan)
[![CodeQL](https://github.com/tomdionysus/manannan/actions/workflows/codeql.yml/badge.svg)](https://github.com/tomdionysus/manannan/actions/workflows/codeql.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

Manannan keeps DNS records pointed at your public IP address. It runs as a
service, checks the address every few minutes and updates any record that is
out of date. It ships with support for AWS Route 53, across any number of
hosted zones and AWS accounts, and takes other providers as plugins.

## Install

On Debian trixie or Raspberry Pi OS (trixie, 64-bit) on arm64:

```sh
sudo curl -fsSLo /etc/apt/sources.list.d/tomcully.sources https://tomcully.com/apt/tomcully.sources
sudo apt update && sudo apt install manannan
```

Elsewhere, build from source: see [docs/building.md](docs/building.md).

## Set up

1. List your records in `/etc/manannan/manannan.yaml`, under
   `registry.aws_route53.records`:

   ```yaml
   records:
     - name: home.example.com
       hosted_zone_id: Z0123456789ABCDEFGHIJ
   ```

2. Add an AWS access key that may update those zones:

   ```sh
   sudoedit /etc/manannan/environment
   ```

   ```
   AWS_ACCESS_KEY_ID=AKIA...
   AWS_SECRET_ACCESS_KEY=...
   ```

3. Start it:

   ```sh
   sudo systemctl enable --now manannan
   ```

## Check it is working

```sh
journalctl -u manannan -f
```

```
[INFO ] managing aws_route53:home.example.com
[INFO ] Manannan 0.3.0 started
[INFO ] set aws_route53:home.example.com to 203.0.113.7
```

A record that is already right is not logged, so a running service with no
`set` lines is normal. Errors name the setting or record at fault.

Updates arrive with `sudo apt upgrade`.

## More

- [Configuration](docs/configuration.md): every setting, several zones and
  accounts, and the minimal IAM policy.
- [Running](docs/running.md): logs, debugging, upgrading and uninstalling.
- [Building from source](docs/building.md), including moving a source install
  to the package.
- [Writing plugins](docs/PLUGINS.md) for other sources and providers.
- [Packaging](docs/packaging.md) and [the name](docs/NAME.md).

## Licence

GPL-3.0-only. See [LICENSE](LICENSE).
