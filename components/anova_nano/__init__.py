"""ESPHome external component for the Anova Nano sous vide cooker (BLE).

Protocol ported from pyanova-nano: https://github.com/filmkorn/pyanova-nano
"""

import esphome.codegen as cg
from esphome.components import ble_client
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@GuyKh"]
DEPENDENCIES = ["ble_client"]
MULTI_CONF = True

CONF_ANOVA_NANO_ID = "anova_nano_id"

anova_nano_ns = cg.esphome_ns.namespace("anova_nano")
AnovaNano = anova_nano_ns.class_(
    "AnovaNano", cg.PollingComponent, ble_client.BLEClientNode
)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(AnovaNano)})
    .extend(cv.polling_component_schema("30s"))  # PyAnova default poll interval
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)

ANOVA_NANO_CHILD_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_ANOVA_NANO_ID): cv.use_id(AnovaNano)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
