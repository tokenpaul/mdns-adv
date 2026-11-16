# mdns-adv

A lightweight, single-process DNS-SD / mDNS service advertiser for **OmniOS** and **illumos** systems.

On illumos-based systems running native `mdnsd` (`svc:/network/dns/multicast:default`), advertising multiple Bonjour services typically requires spawning a separate `dns-sd -R` process (and SMF instance) for each service. `mdns-adv` reads all service definitions from a single configuration file, registers them via `libdns_sd`, drains IPC callbacks cleanly in an event loop, and automatically re-registers if `mdnsd` restarts.

## Features
- **Single-file configuration:** Advertise DNS-SD services (`_smb._tcp`, `_ssh._tcp`, `_device-info._tcp`, `_http._tcp`, etc.) from one SMF service.
- **Event-driven IPC loop:** Uses `select()` and `DNSServiceProcessResult()` to drain messages from `mdnsd` and recover automatically if `mdnsd` restarts.
- **Clean goodbye packets:** Traps `SIGTERM`/`SIGINT` and deallocates all `DNSServiceRef` handles so `mdnsd` transmits `TTL=0` goodbye packets on shutdown.
- **Optional periodic refresh (`-t`):** Supports an optional re-registration timer for networks with aggressive multicast filtering.
- **Unprivileged SMF integration:** Ships with a SMF manifest configured to run as `noaccess` with `basic` privileges.

## Build

Requires GCC and the system `libdns_sd` headers/library (`system/library` / `network/dns/mdns` on OmniOS):

```sh
make
```

## Configuration

Create `/etc/mdnsd.conf` (or pass `-c <path>`). Fields may be separated by **spaces or tabs**; if the service name contains spaces, separate the service name from the remaining fields with a **Tab** character:

```text
# <Name>	<Type>	<Domain>	<Port>	[TXT records]
My NAS Server	_ssh._tcp	local	22
My NAS Server	_device-info._tcp	local	9	model=Xserve
My NAS Server	_smb._tcp	local	445
fileserver _http._tcp local 80 path=/share
```

## SMF Installation (OmniOS / illumos)

1. Install the binary and configuration file (adjust the destination directories if you prefer `/usr/local/bin` or a custom `/opt/...` prefix):
   ```sh
   pfexec cp mdns-adv /usr/bin/mdns-adv
   pfexec cp mdns-adv.conf.sample /etc/mdnsd.conf
   pfexec chmod 0755 /usr/bin/mdns-adv
   pfexec chmod 0644 /etc/mdnsd.conf
   ```

2. **Review and adjust `mdns-adv.xml` before importing:**
   - Update the binary path inside `<exec_method name="start" ...>` if you installed `mdns-adv` somewhere other than `/usr/bin/mdns-adv`.
   - Optionally adjust the default values in `<property_group name="config">` (`config/config`, `config/timer`, `config/debug`) or `<method_credential>` (`user`/`group`) to match your environment.

3. Validate, import, and enable the SMF manifest:
   ```sh
   pfexec svccfg validate mdns-adv.xml
   pfexec svccfg import mdns-adv.xml
   pfexec svcadm enable svc:/network/dns/mdns-adv:default
   ```

4. Adjust runtime properties anytime via `svccfg` without editing the XML file:
   ```sh
   # Enable verbose debug logging
   pfexec svccfg -s svc:/network/dns/mdns-adv:default setprop config/debug = astring: "1"
   pfexec svcadm refresh svc:/network/dns/mdns-adv:default
   pfexec svcadm restart svc:/network/dns/mdns-adv:default
   ```

## Command-Line Options

```text
Usage: mdns-adv [options]

Options:
  -c <path>      Specify configuration file path (default: /etc/mdnsd.conf)
  -t <minutes>   Set re-registration interval in minutes (default: 0, disabled)
  -d, --debug    Enable verbose debugging output
  -h, --help     Show this help and usage message
```

## License

Common Development and Distribution License 1.0 (`CDDL-1.0`). See `LICENSE` for details.

