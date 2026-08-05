# Proposal: IPv4 routing and static port mapping component for ESPHome

**For:** ESPHome maintainers
**Status:** Request for comment before opening a PR
**Reference implementation:** `ip_router` external component — written, not yet hardware-tested

---

## Summary

Add a component that lets an ESP32 route IPv4 between two network interfaces, with static inbound port maps, so that a multi-MCU device can present a **single IP address** to Home Assistant while each MCU keeps its own ESPHome API connection.

ESP-IDF already provides everything needed. Nothing here requires patching lwIP. The work is a configuration schema, netif wiring, and lifecycle handling.

---

## The problem

ESPHome devices increasingly use more than one MCU. A device with an audio processor and a sensor processor, or a display controller and a radio controller, has two ESPHome nodes inside one physical product.

Today there are two options, and both are unsatisfying:

**Option 1 — one node has no network.** Link the two MCUs over UART with the Packet Transport component. This works for relaying sensor values, and it is what we do now. But packet transport cannot carry an API connection, so the second MCU cannot run `bluetooth_proxy`, cannot be an OTA target, cannot expose its own entities, and cannot serve a display with live Home Assistant data.

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
      target_port: 6053
```

Home Assistant reaches the router's own API on `192.168.1.50:6053` and the second MCU's API on `192.168.1.50:6054`.

| Option | Type | Notes |
|---|---|---|
| `upstream` | string, required | `esp_netif` ifkey of the LAN-facing interface. Its address becomes the external address for port maps. |
| `downstream` | string, required | `esp_netif` ifkey of the private interface. NAPT is enabled here. |
| `port_forward` | list, optional | Static inbound maps. |

Interfaces are referenced by `esp_netif` ifkey rather than by ESPHome component ID, so the component is agnostic about how the second interface was created — the `ethernet` component, a switch-IC driver, or PPP.

---

## Implementation

Roughly 300 lines. The reference implementation exists and is attached to this proposal.

**Build configuration**, set from Python:

- `CONFIG_LWIP_IP_FORWARD`
- `CONFIG_LWIP_IPV4_NAPT`
- `CONFIG_LWIP_IPV4_NAPT_PORTMAP`

All three are existing ESP-IDF Kconfig options. Older community guides that patch `lwipopts.h` or `opt.h` by hand predate them.

**Runtime:**

- `esp_netif_get_handle_from_ifkey()` to resolve interfaces
- `esp_netif_napt_enable()` on the downstream interface
- `ip_portmap_add(proto, maddr, mport, daddr, dport)` per rule, from `lwip/lwip_napt.h`

**Lifecycle:** interfaces are generally not up when `setup()` runs, so the work is retried from `loop()` on a 2 s interval with a bounded attempt count, then reported via `status_set_error()`.

**One detail worth flagging for reviewers:** ESP-IDF enables NAPT on the interface connecting to the target network — their own example enables it on the Ethernet interface to give Ethernet clients internet access via WiFi. That is the *downstream* side, which is the opposite of the usual NAT mental model. Getting it backwards fails silently, so it is worth documenting prominently whatever shape the component ends up taking.

**Reference:** ESP-IDF's `network/vlan_support` example demonstrates NAPT between interfaces.

---

## Scope boundaries

Deliberately excluded:

- **Creating the second interface.** That belongs to the Ethernet driver, a switch-IC driver, or PPP. This component only routes between interfaces that already exist.
- Dynamic rules, UPnP, firewalling, IPv6.

---

## Known gap: mDNS

A node behind the router advertises a private address that Home Assistant cannot reach, so it must be found by `use_address` on the router's LAN address at the mapped port.

**Question for maintainers: does `use_address` accept a `host:port` form?** If not, this proposal needs either a small extension there, or the router component needs to proxy mDNS for the downstream segment. This is the one part of the design that may need changes outside the new component, and it would be good to settle before a PR is opened.

---

## Why this belongs in ESPHome rather than staying an external component

It can stay external, and it will start there. Two arguments for eventually adopting it:

1. **Multi-MCU ESPHome devices are becoming normal**, and the current answer — one node goes mute, or the product takes two addresses — is a workaround rather than a design.
2. **The primitives are already in ESP-IDF.** ESPHome is not taking on a networking stack; it is exposing three Kconfig options and two function calls behind a schema.

Reviewers may reasonably feel a device framework should not ship a router. That is a fair objection, and the counter-argument is the concrete use case rather than generality: a single product, a single cable, a single address, two processors that each need to talk to Home Assistant.

---

## Status and next steps

The reference implementation is written but **has not been tested on hardware**. It will be validated on an ESP32-S31 pair with a KSZ8863 switch in Port Mode, which gives two independent `esp_netif` instances from one EMAC.

Specific unknowns that testing will resolve:

- Whether `esp_netif_napt_enable()` behaves correctly on ESP-IDF 6.x. Reports of missing symbols, crashes in `ip_napt_deinit()`, and routing failures all date from the 4.x era.
- Throughput for a routed audio stream.
- `use_address` with a non-default port.

**We are not asking for a merge on untested code.** This is a request for comment on the approach and the schema before that work is done, so that any redesign happens now rather than after validation.

---

## Use case background

A two-MCU voice satellite for Home Assistant: an audio processor handling an XMOS front end, wake word, media pipeline and a touch display; a sensor processor handling mmWave presence, environmental sensors, an LED ring, and the 2.4 GHz radio roles — BLE proxy, Thread FTD router, or Zigbee router.

The radio MCU needs its own API connection because `bluetooth_proxy` requires one. The device has one PoE cable and should have one address.
