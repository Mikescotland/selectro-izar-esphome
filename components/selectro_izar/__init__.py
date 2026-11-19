"""Selectro IZAR Gateway: factory-key PRIOS auto-discovery with web setup.

The custom C++ component is intentionally separate from wmbus_meter, which
requires a compile-time meter ID. Receive frames via wmbus_radio.on_frame.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "web_server_base", "wmbus_radio"]
CODEOWNERS = []

selectro_ns = cg.esphome_ns.namespace("selectro_izar")
Gateway = selectro_ns.class_("Gateway", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(Gateway),
    cv.Optional("admin_username", default=""): cv.string_strict,
    cv.Optional("admin_password", default=""): cv.string_strict,
}).extend(
    cv.COMPONENT_SCHEMA
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_admin_credentials(config["admin_username"], config["admin_password"]))
