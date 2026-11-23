# Changelog

Newest first.

## Unreleased (0.4.0)

Breaking: the configuration and the package layout change. An upgrade of
the package migrates a 0.3 installation (see `docs/running.md`).

- The configuration is `/etc/manannan/config.yaml`, and keys go in
  `/etc/manannan/secrets.yaml`, merged over it and readable by the service
  alone. The environment file is gone.
- Any setting can be overridden by `MANANNAN_<KEY>` in the environment or
  `--<key>` on the command line; `--config` and `--secrets` name the files.
- `log.level` (`all`, `debug`, `info`, `warn`, `error`) replaces
  `manannan.log_level` and `LOG_LEVEL`; `log.format: json` writes one JSON
  object per line.
- `manannan migrate` converts a 0.3 installation's files.
- The daemon is `/usr/bin/manannan` and runs as the `manannan` system user.
- A failing record, or the source, backs off (doubling from one cadence to
  64) and is parked after 8 failures in a row; it is still retried every 64
  cadences and at once when the address changes. `systemctl reload manannan`
  retries everything now.
- AWS keys are held in locked memory that is wiped on release, and are never
  logged.
- Route 53 responses are capped at 1 MiB and check IP bodies at 64 bytes.
  Record names with an escape that is not an octal byte no longer fail the
  lookup.
- `manannan-dev` installs only the plugin interface.

## 0.3.0

- Debian packages for trixie arm64, a hardened systemd unit, a manual page and
  `--version`.
- A use after `dlclose` in duplicate plugin detection fixed.

## 0.2.0

- Registries manage any number of records, each reconciled on its own;
  Route 53 records across zones and accounts, with per-record credentials and
  `${NAME}` references to the environment (plugin ABI 2).
- Keys moved from the configuration to an environment file.

## 0.1.0

- The daemon, the plugin loader, the amazon_check_ip source and the
  aws_route53 registry with SigV4.
