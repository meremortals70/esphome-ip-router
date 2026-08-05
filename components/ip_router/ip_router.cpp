#include "ip_router.h"

#ifdef USE_ESP32

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "esp_netif.h"
#include "lwip/ip4_addr.h"

extern "C" {
#include "lwip/lwip_napt.h"
}

namespace esphome {
namespace ip_router {

static const char *const TAG = "ip_router";

/// Interfaces may not be up when setup() runs. Retry on this interval until
/// they are, then stop.
static const uint32_t RETRY_INTERVAL_MS = 2000;
static const uint8_t MAX_ATTEMPTS = 60;

float IPRouter::get_setup_priority() const {
  // After network components have created their esp_netif instances.
  return setup_priority::AFTER_WIFI;
}

void IPRouter::add_port_map(Protocol protocol, uint16_t external_port, const std::string &target_address,
                            uint16_t target_port) {
  PortMap map{};
  map.protocol = protocol;
  map.external_port = external_port;
  map.target_address = target_address;
  map.target_port = target_port;
  this->port_maps_.push_back(map);
}

void IPRouter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up IP router...");
  // Work is done in loop() because the interfaces are usually not up yet.
}

void IPRouter::loop() {
  if (this->is_active())
    return;

  if (this->attempts_ >= MAX_ATTEMPTS) {
    if (this->attempts_ == MAX_ATTEMPTS) {
      ESP_LOGE(TAG, "Giving up after %u attempts; routing is NOT active", MAX_ATTEMPTS);
      this->attempts_++;
      this->status_set_error("Failed to enable routing");
    }
    return;
  }

  const uint32_t now = millis();
  if (this->last_attempt_ != 0 && (now - this->last_attempt_) < RETRY_INTERVAL_MS)
    return;
  this->last_attempt_ = now;
  this->attempts_++;

  if (!this->napt_enabled_ && !this->enable_napt_())
    return;

  if (!this->port_maps_installed_ && !this->install_port_maps_())
    return;

  ESP_LOGI(TAG, "Routing active: %s -> %s, %u port map(s)", this->downstream_ifkey_.c_str(),
           this->upstream_ifkey_.c_str(), (unsigned) this->port_maps_.size());
  this->status_clear_error();
}

bool IPRouter::enable_napt_() {
  esp_netif_t *downstream = esp_netif_get_handle_from_ifkey(this->downstream_ifkey_.c_str());
  if (downstream == nullptr) {
    ESP_LOGD(TAG, "Downstream interface '%s' not found yet", this->downstream_ifkey_.c_str());
    return false;
  }

  if (!esp_netif_is_netif_up(downstream)) {
    ESP_LOGD(TAG, "Downstream interface '%s' is not up yet", this->downstream_ifkey_.c_str());
    return false;
  }

  // ESP-IDF enables NAPT on the interface facing the network whose traffic is
  // being translated, i.e. the downstream/private side, not the upstream side.
  const esp_err_t err = esp_netif_napt_enable(downstream);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_netif_napt_enable() on '%s' failed: %s", this->downstream_ifkey_.c_str(),
             esp_err_to_name(err));
    return false;
  }

  ESP_LOGD(TAG, "NAPT enabled on '%s'", this->downstream_ifkey_.c_str());
  this->napt_enabled_ = true;
  return true;
}

bool IPRouter::upstream_address_(uint32_t *addr_out) {
  esp_netif_t *upstream = esp_netif_get_handle_from_ifkey(this->upstream_ifkey_.c_str());
  if (upstream == nullptr) {
    ESP_LOGD(TAG, "Upstream interface '%s' not found yet", this->upstream_ifkey_.c_str());
    return false;
  }

  esp_netif_ip_info_t info{};
  const esp_err_t err = esp_netif_get_ip_info(upstream, &info);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_netif_get_ip_info() on '%s' failed: %s", this->upstream_ifkey_.c_str(),
             esp_err_to_name(err));
    return false;
  }

  if (info.ip.addr == 0) {
    ESP_LOGD(TAG, "Upstream interface '%s' has no address yet", this->upstream_ifkey_.c_str());
    return false;
  }

  *addr_out = info.ip.addr;
  return true;
}

bool IPRouter::install_port_maps_() {
  if (this->port_maps_.empty()) {
    this->port_maps_installed_ = true;
    return true;
  }

  uint32_t external_addr = 0;
  if (!this->upstream_address_(&external_addr))
    return false;

  bool all_installed = true;
  for (auto &map : this->port_maps_) {
    if (map.installed)
      continue;

    const uint32_t target_addr = ipaddr_addr(map.target_address.c_str());
    if (target_addr == IPADDR_NONE) {
      ESP_LOGE(TAG, "Invalid target address '%s'", map.target_address.c_str());
      map.installed = true;  // never going to succeed; do not retry forever
      continue;
    }

    ip_portmap_add((u8_t) map.protocol, external_addr, map.external_port, target_addr, map.target_port);
    map.installed = true;

    ESP_LOGD(TAG, "Mapped %s/%u -> %s:%u", map.protocol == PROTOCOL_TCP ? "tcp" : "udp",
             map.external_port, map.target_address.c_str(), map.target_port);
  }

  for (const auto &map : this->port_maps_) {
    if (!map.installed)
      all_installed = false;
  }

  this->port_maps_installed_ = all_installed;
  return all_installed;
}

void IPRouter::dump_config() {
  ESP_LOGCONFIG(TAG, "IP Router:");
  ESP_LOGCONFIG(TAG, "  Upstream interface: %s", this->upstream_ifkey_.c_str());
  ESP_LOGCONFIG(TAG, "  Downstream interface: %s", this->downstream_ifkey_.c_str());
  ESP_LOGCONFIG(TAG, "  NAPT enabled: %s", YESNO(this->napt_enabled_));
  ESP_LOGCONFIG(TAG, "  Port forwards: %u", (unsigned) this->port_maps_.size());
  for (const auto &map : this->port_maps_) {
    ESP_LOGCONFIG(TAG, "    %s/%u -> %s:%u (%s)", map.protocol == PROTOCOL_TCP ? "tcp" : "udp",
                  map.external_port, map.target_address.c_str(), map.target_port,
                  map.installed ? "installed" : "pending");
  }
}

}  // namespace ip_router
}  // namespace esphome

#endif  // USE_ESP32
