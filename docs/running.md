# Running

## The service

```sh
sudo systemctl enable --now manannan   # start now and at every boot
sudo systemctl restart manannan        # after changing the configuration or secrets
journalctl -u manannan -f              # follow the log
```

The service starts at boot once the network is online. It runs as the
`manannan` system user with no capabilities, a read-only view of the system
and a restricted set of system calls.

## Logs

At `info`, Manannan logs plugin loads, the records it manages and each change
it makes. Records that are already current are logged only at `debug`:

```
[INFO ] loaded plugin amazon_check_ip
[INFO ] loaded plugin aws_route53
[INFO ] managing aws_route53:home.example.com
[INFO ] Manannan 0.4.0 started
[INFO ] set aws_route53:home.example.com to 203.0.113.7
```

With `log.format: json` each line is a JSON object with `time`, `level` and
`message`.

Configuration errors stop startup with a message naming the offending setting:
unknown keys, duplicate records, missing zones and incomplete credentials are
all rejected.

Runtime failures, such as a provider returning an error or a network timeout,
are logged against the record (or the source) with when it will be tried
again. Each failure in a row doubles the wait, from one cadence up to 64. After
8 failures in a row the record is parked, which the log says once:

```
[ERROR] aws_route53:home.example.com: Route 53 returned HTTP 403: ...; parked after 8 failures: retrying every 5h20m, when the value changes, or at once on SIGHUP
```

A parked record is still tried every 64 cadences, and at once when the
address changes, so it recovers on its own. To try every waiting record now,
after fixing the cause:

```sh
sudo systemctl reload manannan
```

A record that succeeds again logs that it recovered.

For a full trace without editing the configuration:

```sh
sudo systemctl set-environment MANANNAN_LOG_LEVEL=debug
sudo systemctl restart manannan
# ... read the log, then
sudo systemctl unset-environment MANANNAN_LOG_LEVEL
sudo systemctl restart manannan
```

## Upgrading

```sh
sudo apt update && sudo apt upgrade
```

An upgrade restarts the service if it is running, and keeps the configuration
and secrets.

An upgrade from 0.3 moves `manannan.yaml` to `config.yaml` (with `log_level`
becoming `log.level`) and the keys from `/etc/manannan/environment` to
`secrets.yaml`, then removes the old files. Comments in the old file are not
carried over. If the environment file sets anything other than the `AWS_*`
keys and `LOG_LEVEL`, the upgrade leaves both old files in place and says
what to move by hand; then run the migration again:

```sh
sudo manannan migrate /etc/manannan/manannan.yaml.pre-0.4.0 /etc/manannan/environment \
  /etc/manannan/config.yaml /etc/manannan/secrets.yaml
sudo chown manannan:manannan /etc/manannan/secrets.yaml
```

## Uninstalling

```sh
sudo apt remove manannan   # keeps /etc/manannan
sudo apt purge manannan    # also deletes /etc/manannan, secrets included
```
