# Running

## The service

```sh
sudo systemctl enable --now manannan   # start now and at every boot
sudo systemctl restart manannan        # after changing the configuration or keys
journalctl -u manannan -f              # follow the log
```

The service starts at boot once the network is online. It runs as an
unprivileged dynamic user with no capabilities, a read-only view of the
system and a restricted set of system calls.

## Logs

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
all rejected. Runtime failures, such as a provider returning an error or a
network timeout, are logged against the record and retried on the next cycle.

For a full trace, add `LOG_LEVEL=debug` to `/etc/manannan/environment` and
restart. It overrides `log_level` in the configuration. Remove it and restart
to go back.

## Upgrading

```sh
sudo apt update && sudo apt upgrade
```

An upgrade restarts the service if it is running, and keeps the configuration
and keys.

## Uninstalling

```sh
sudo apt remove manannan   # keeps /etc/manannan
sudo apt purge manannan    # also deletes /etc/manannan, keys included
```
