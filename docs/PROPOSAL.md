# Proposal: IPv4 routing and static port mapping component for ESPHome

**For:** ESPHome maintainers
**Status:** Request for comment before opening a PR
**Reference implementation:** [`ip_router`](../components/ip_router) — config validation passes on ESPHome 2026.7.3; C++ not yet compiled or hardware-tested

---

## Summary

Add a component that lets an ESP32 route IPv4 between two network interfaces, with static inbound port maps, so that a multi-MCU device can present a **single IP address** to Home Assistant while each MCU keeps its own ESPHome API connection.

ESP-IDF already provides everything needed. Nothing here requires patching lwIP, and nothing here requires changes to existing ESPHome components. The work is a configuration schema, netif wiring, and lifecycle handling.

---

## The problem

ESPHome devices increasingly use more than one MCU. A device with an audio processor and a sensor processor, or a display controller and a radio controller, is two ESPHome nodes inside one physical product.

Today there are two options, and both are unsatisfying:

**Option 1 — one node has no network.** Link the two MCUs over UART with the Packet Transport component. This works for relaying sensor values. But packet transport cannot carry an API connection, so the second MCU cannot run `bluetooth_proxy`, cannot be an OTA target, cannot expose its own entities, and cannot drive a display with live Home Assistant data.

**Option 2 — two IP addresses.** Give both MCUs network access. Functionally fine, but the product is now two entries in the ESPHome dashboard, two DHCP reservations, and two devices for a user who bought one.

**NAT does not solve this.** The ESPHome API is a *server* on the device — Home Assistant connects inward. NAT is outbound-only, so a node behind it is invisible.

**PAT does solve it.** Static port mapping means one address is advertised and each MCU's API is reachable on its own port behind it.

---

## Proposed configuration

```yaml
ip_router:
  upstream: "ETH_DEF"
  downstream: "ETH_SPI_0"
  port_forward:
    - protocol: tcp
      external_port: 6054
      target_address: 10.99.0.2
      target_port: 6054
```

| Option | Type | Notes |
|---|---|---|
| `upstream` | string, required | `esp_netif` ifkey of the LAN-facing interface. Its address becomes the external address for port maps. |
| `downstream` | string, required | `esp_netif` ifkey of the private interface. NAPT is enabled here. |
| `port_forward` | list, optional | Static inbound maps. |
| `port_forward[].protocol` | `tcp` \| `udp` | Defaults to `tcp`. |
| `port_forward[].external_port` | port | Port on the upstream address. |
| `port_forward[].target_address` | IPv4 | Host on the downstream segment. |
| `port_forward[].target_port` | port | Port on the target host. |

Interfaces are referenced by `esp_netif` ifkey rather than by ESPHome component ID, so the component is agnostic about how the second interface was created — the `ethernet` component, a switch-IC driver, or PPP.

---

## Implementation

Roughly 300 lines across `__init__.py`, a header and a source file. The reference implementation is in this repository.

**Build configuration**, set from Python via `add_idf_sdkconfig_option`:

- `CONFIG_LWIP_IP_FORWARD`
- `CONFIG_LWIP_IPV4_NAPT`
- `CONFIG_LWIP_IPV4_NAPT_PORTMAP`

All three are existing ESP-IDF Kconfig options. Community guides that patch `lwipopts.h` or `opt.h` by hand predate them.

**Runtime:**

- `esp_netif_get_handle_from_ifkey()` to resolve interfaces
- `esp_netif_napt_enable()` on the downstream interface
- `ip_portmap_add(proto, maddr, mport, daddr, dport)` per rule, from `lwip/lwip_napt.h`

**Lifecycle:** interfaces are generally not up when `setup()` runs, so the work is retried from `loop()` on a 2 s interval with a bounded attempt count, then reported through `status_set_error()`.

**One detail worth flagging for reviewers:** ESP-IDF enables NAPT on the interface connecting to the target network — their own example enables it on the Ethernet interface to give Ethernet clients internet access via WiFi. That is the *downstream* side, which is the opposite of the usual NAT mental model. Getting it backwards fails silently, so it warrants prominent documentation whatever shape the component ends up taking.

**Reference:** ESP-IDF's `network/vlan_support` example demonstrates NAPT between interfaces.

---

## Scope boundaries

Deliberately excluded:

- **Creating the second interface.** That belongs to the Ethernet driver, a switch-IC driver, or PPP. This component only routes between interfaces that already exist.
- Dynamic rules, UPnP, firewalling, IPv6.

---

## Service discovery — investigated, no upstream change needed

An earlier draft of this proposal asked whether `use_address` accepts a `host:port` form. **It does not.** In `network/__init__.py`, `add_use_address()` takes a bare string and passes it to `set_use_address()`; there is no port handling anywhere in that path.

**This turns out not to matter**, because both services on the downstream node already have configurable ports — `api:` takes `port` (default 6053) and the `esphome` OTA platform takes `port` (default 3232). So the downstream node can simply run its services on non-default ports, and the maps become 1:1:

```yaml
# Downstream node
api:
  port: 6054
ota:
  - platform: esphome
    port: 3233
```

```yaml
# Router node
ip_router:
  upstream: "ETH_DEF"
  downstream: "ETH_SPI_0"
  port_forward:
    - {protocol: tcp, external_port: 6054, target_address: 10.99.0.2, target_port: 6054}
    - {protocol: tcp, external_port: 3233, target_address: 10.99.0.2, target_port: 3233}
```

With 1:1 maps, pointing `use_address` at the router's LAN address is sufficient — the port is already correct on both sides.

**What remains manual:** mDNS. The downstream node advertises an address on the private segment that Home Assistant cannot route to, so it will not be auto-discovered and must be added by host and port through the ESPHome integration's config flow. That is acceptable for a two-node product, and it is the one rough edge in the design.

**A possible future improvement, not requested here:** the router node could proxy mDNS for the downstream segment, rewriting advertised addresses to its own. That is materially more complex than this component and is better considered separately, if at all.

---

## Why this might belong in ESPHome rather than staying external

It can stay external, and it starts there. Two arguments for eventual adoption:

1. **Multi-MCU ESPHome devices are becoming normal**, and the current answer — one node goes mute, or the product takes two addresses — is a workaround rather than a design.
2. **The primitives are already in ESP-IDF.** ESPHome would not be taking on a networking stack; it would be exposing three Kconfig options and two function calls behind a schema.

Reviewers may reasonably feel a device framework should not ship a router. That is a fair objection, and the counter-argument is the concrete use case rather than generality: one product, one cable, one address, two processors that each need to talk to Home Assistant.

---

## Status and next steps

**Verified:** the component loads and validates on ESPHome 2026.7.3, including its custom validators, via `esphome config` and through the Device Builder add-on.

**Not verified:** any of the C++. `esphome config` does not compile.

Specific unknowns that hardware testing will resolve:

- Whether `esp_netif_napt_enable()` behaves correctly on ESP-IDF 6.x. Reports of missing symbols, crashes in `ip_napt_deinit()`, and routing failures requiring `lwipopts.h` edits all date from the 4.x era and may no longer apply.
- Throughput for a routed audio stream.
- Behaviour with a specific second-interface driver.

Validation is planned on an ESP32-S31 pair with a KSZ8863 switch in Espressif's Port Mode, which yields two independent `esp_netif` instances from a single EMAC.

**We are not asking for a merge on unbuilt code.** This is a request for comment on the approach and the schema before that work is done, so that any redesign happens now rather than after validation.

---

## Use case background

A two-MCU voice satellite for Home Assistant: an audio processor handling an XMOS front end, wake word, media pipeline and a touch display; a sensor processor handling mmWave presence, environmental sensors, an LED ring, and the 2.4 GHz radio roles — BLE proxy, Thread FTD router, or Zigbee router.

The radio MCU needs its own API connection because `bluetooth_proxy` requires one. The device has one PoE cable and should have one address.
