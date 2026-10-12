import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import ANOVA_NANO_CHILD_SCHEMA, CONF_ANOVA_NANO_ID

DEPENDENCIES = ["anova_nano"]

CONF_FIRMWARE_VERSION = "firmware_version"
CONF_FIRMWARE_COMMIT = "firmware_commit"
CONF_DEVICE_INFO = "device_info"
CONF_UNIT = "unit"

TEXT_SENSORS = {
    CONF_FIRMWARE_VERSION: text_sensor.text_sensor_schema(
        icon="mdi:chip", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    CONF_FIRMWARE_COMMIT: text_sensor.text_sensor_schema(
        icon="mdi:source-commit", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    # Raw GET_DEVICE_INFO value; pyanova-nano doesn't document its meaning.
    CONF_DEVICE_INFO: text_sensor.text_sensor_schema(
        icon="mdi:information-outline", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    CONF_UNIT: text_sensor.text_sensor_schema(
        icon="mdi:temperature-celsius", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
}

CONFIG_SCHEMA = ANOVA_NANO_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ANOVA_NANO_ID])
    for key in TEXT_SENSORS:
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(hub, f"set_{key}_text_sensor")(sens))
