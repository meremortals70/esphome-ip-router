# esphome-ip-router

An ESPHome external component that turns an ESP32 into an IPv4 router: it enables
forwarding and NAPT between two network interfaces and installs static inbound
port maps.

**Status: written, not tested. No hardware validation has been performed.**
**Note config validation passes on 2026.7.3 and the C++ remains unbuilt.**

See the [proposal to the ESPHome team](docs/Proposal.md) for the rationale.

## Why

ESPHome devices with two MCUs normally need two IP addresses, because the
ESPHome native API is a *server* on the device — Home Assistant connects
inward. NAT alone hides the device. Static port mapping (PAT) does not: one
address is advertised, and each MCU's API is reachable on its own port behind it.

```
                  ┌──────────────────────────────────────┐
   LAN ───────────┤ upstream netif    (DHCP, 192.168.1.x) │
                  │                                       │
                  │            ip_router                  │
                  │      forwarding + NAPT + portmap      │
                  │                                       │
                  │ downstream netif  (static, 10.99.0.1) ├─── second MCU
                  └──────────────────────────────────────┘         10.99.0.2
```

Home Assistant reaches the router's own API on `192.168.1.50:6053` and the
second MCU's API on `192.168.1.50:6054`.

## Configuration

```yaml
external_components:
  - source: github://meremortals70/esphome-ip-router
    components: [ip_router]

ip_router:
  upstream: "ETH_DEF"
  downstream: "ETH_SPI_0"
  port_forward:
    - protocol: tcp
      external_port: 6054
      target_address: 10.99.0.2
      target_port: 6053
```

### Options

| Option | Type | Notes |
|---|---|---|
| `upstream` | string, **required** | `esp_netif` ifkey of the LAN-facing interface. Its address is used as the external address for port maps. |
| `downstream` | string, **required** | `esp_netif` ifkey of the private interface. NAPT is enabled here. |
| `port_forward` | list, optional | Static inbound maps. |
| `port_forward[].protocol` | `tcp` or `udp` | Defaults to `tcp`. |
| `port_forward[].external_port` | port | Port on the upstream address. |
| `port_forward[].target_address` | IPv4 | Host on the downstream segment. |
| `port_forward[].target_port` | port | Port on the target host. |

### Interface keys

Interfaces are referenced by `esp_netif` ifkey, so the component does not care
how they were created. Common keys are `ETH_DEF`, `WIFI_STA_DEF`,
`WIFI_AP_DEF`. Additional interfaces created by other drivers use whatever key
that driver registers. If the key is wrong, the log shows
`interface '<key>' not found yet` and retries.

## What it does not do

- **It does not create the second interface.** That is the job of the Ethernet
  driver, a switch IC driver, or PPP. This component only routes between
  interfaces that already exist.
- **It does not fix mDNS.** The downstream device advertises a private address
  that Home Assistant cannot route to. Set `use_address` on that device to the
  upstream IP. Whether ESPHome's `use_address` accepts a non-default port is
  unverified.
- No dynamic rules, no UPnP, no firewalling.

## Build configuration

The component sets three ESP-IDF options automatically:

- `CONFIG_LWIP_IP_FORWARD` — prerequisite for NAPT on the ESP platform
- `CONFIG_LWIP_IPV4_NAPT`
- `CONFIG_LWIP_IPV4_NAPT_PORTMAP` — required for inbound static maps

No lwIP source patching is required. Older guides that patch `opt.h` by hand
predate these Kconfig options.

## Implementation notes

**NAPT goes on the downstream interface.** ESP-IDF documents that NAPT is
enabled on the interface connecting to the target network — their own example
enables it on Ethernet to give Ethernet clients internet access via WiFi. This
is the opposite of the usual mental model, where NAT sits on the outside edge.
Getting it backwards fails silently.

**NAPT can only be enabled on one interface at a time**, per ESP-IDF's
documentation. That suits this use case and rules out multi-segment routing.

**Work happens in `loop()`, not `setup()`**, because interfaces are usually not
up when components are set up. The component retries every 2 s for up to 60
attempts, then reports a component error.

**`IP_PORTMAP_MAX` defaults to 32 entries** in lwIP.

## Untested

Everything. In particular:

- Whether `esp_netif_napt_enable()` behaves correctly on ESP-IDF 6.x. Reports of
  missing symbols, crashes in `ip_napt_deinit()`, and routing failures all date
  from the 4.x era and may or may not still apply.
- Throughput for a routed media stream.
- `use_address` with a non-default port.
- Behaviour with any specific second-interface driver.

## Reference

ESP-IDF's `network/vlan_support` example demonstrates NAPT between interfaces
and is the closest working reference.

## Licence

Apache 2.0, matching ESPHome.

## Credits

Co-Authored with Claude (Anthropic), to a design and specification by Jason Wienert.
