import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_MOISTURE,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
)

from . import ANOVA_NANO_CHILD_SCHEMA, CONF_ANOVA_NANO_ID

DEPENDENCIES = ["anova_nano"]

CONF_RUNNING = "running"
CONF_WATER_LOW = "water_low"
CONF_WATER_LEAK = "water_leak"

BINARY_SENSORS = {
    # PyAnova.get_status(): running iff motor_speed != 0
    CONF_RUNNING: binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    CONF_WATER_LOW: binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM, icon="mdi:water-alert"
    ),
    CONF_WATER_LEAK: binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_MOISTURE
    ),
}

CONFIG_SCHEMA = ANOVA_NANO_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in BINARY_SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ANOVA_NANO_ID])
    for key in BINARY_SENSORS:
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(hub, f"set_{key}_binary_sensor")(sens))
