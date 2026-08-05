"""IP router component for ESPHome.

Enables IPv4 forwarding and NAPT between two network interfaces on an ESP32,
with static port maps so an upstream controller can reach devices on the
private segment through a single advertised address.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option, only_on_esp32
from esphome.const import CONF_ID, CONF_PROTOCOL

CODEOWNERS = ["@meremortals"]
DEPENDENCIES = ["esp32"]

CONF_UPSTREAM = "upstream"
CONF_DOWNSTREAM = "downstream"
CONF_PORT_FORWARD = "port_forward"
CONF_EXTERNAL_PORT = "external_port"
CONF_TARGET_ADDRESS = "target_address"
CONF_TARGET_PORT = "target_port"

ip_router_ns = cg.esphome_ns.namespace("ip_router")
IPRouter = ip_router_ns.class_("IPRouter", cg.Component)

Protocol = ip_router_ns.enum("Protocol")
PROTOCOLS = {
    "tcp": Protocol.PROTOCOL_TCP,
    "udp": Protocol.PROTOCOL_UDP,
}

PORT_FORWARD_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PROTOCOL, default="tcp"): cv.enum(PROTOCOLS, lower=True),
        cv.Required(CONF_EXTERNAL_PORT): cv.port,
        cv.Required(CONF_TARGET_ADDRESS): cv.ipv4address,
        cv.Required(CONF_TARGET_PORT): cv.port,
    }
)


def _validate_unique_external_ports(config):
    """Two rules on the same protocol and external port would silently
    overwrite one another in the lwIP portmap table."""
    seen = set()
    for rule in config[CONF_PORT_FORWARD]:
        key = (rule[CONF_PROTOCOL], rule[CONF_EXTERNAL_PORT])
        if key in seen:
            raise cv.Invalid(
                f"Duplicate port forward for {rule[CONF_PROTOCOL]} external port "
                f"{rule[CONF_EXTERNAL_PORT]}"
            )
        seen.add(key)
    return config


def _validate_interfaces_differ(config):
    if config[CONF_UPSTREAM] == config[CONF_DOWNSTREAM]:
        raise cv.Invalid(
            "upstream and downstream must be different interfaces; routing "
            "between an interface and itself is not supported"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(IPRouter),
            cv.Required(CONF_UPSTREAM): cv.string_strict,
            cv.Required(CONF_DOWNSTREAM): cv.string_strict,
            cv.Optional(CONF_PORT_FORWARD, default=[]): cv.ensure_list(
                PORT_FORWARD_SCHEMA
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_unique_external_ports,
    _validate_interfaces_differ,
    only_on_esp32,
    cv.only_with_esp_idf,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_upstream_ifkey(config[CONF_UPSTREAM]))
    cg.add(var.set_downstream_ifkey(config[CONF_DOWNSTREAM]))

    for rule in config[CONF_PORT_FORWARD]:
        cg.add(
            var.add_port_map(
                rule[CONF_PROTOCOL],
                rule[CONF_EXTERNAL_PORT],
                str(rule[CONF_TARGET_ADDRESS]),
                rule[CONF_TARGET_PORT],
            )
        )

    # IP_FORWARD is a prerequisite for IP_NAPT on the ESP platform.
    # IP_NAPT_PORTMAP is required for inbound static port maps.
    add_idf_sdkconfig_option("CONFIG_LWIP_IP_FORWARD", True)
    add_idf_sdkconfig_option("CONFIG_LWIP_IPV4_NAPT", True)
    add_idf_sdkconfig_option("CONFIG_LWIP_IPV4_NAPT_PORTMAP", True)
