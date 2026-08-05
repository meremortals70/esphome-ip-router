#pragma once

#ifdef USE_ESP32

#include <string>
#include <vector>

#include "esphome/core/component.h"

namespace esphome {
namespace ip_router {

enum Protocol : uint8_t {
  PROTOCOL_TCP = 6,
  PROTOCOL_UDP = 17,
};

struct PortMap {
  Protocol protocol;
  uint16_t external_port;
  std::string target_address;
  uint16_t target_port;
  bool installed{false};
};

/// Enables IPv4 forwarding and NAPT between two esp_netif interfaces, and
/// installs static inbound port maps.
///
/// Interfaces are referenced by their esp_netif ifkey (for example "ETH_DEF")
/// so this component does not care how they were created.
class IPRouter : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_upstream_ifkey(const std::string &ifkey) { this->upstream_ifkey_ = ifkey; }
  void set_downstream_ifkey(const std::string &ifkey) { this->downstream_ifkey_ = ifkey; }

  void add_port_map(Protocol protocol, uint16_t external_port, const std::string &target_address,
                    uint16_t target_port);

  /// True once NAPT is enabled and every port map has been installed.
  bool is_active() const { return this->napt_enabled_ && this->port_maps_installed_; }

 protected:
  bool enable_napt_();
  bool install_port_maps_();
  bool upstream_address_(uint32_t *addr_out);

  std::string upstream_ifkey_;
  std::string downstream_ifkey_;
  std::vector<PortMap> port_maps_;

  bool napt_enabled_{false};
  bool port_maps_installed_{false};
  uint32_t last_attempt_{0};
  uint8_t attempts_{0};
};

}  // namespace ip_router
}  // namespace esphome

#endif  // USE_ESP32
