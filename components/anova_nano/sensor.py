import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_INTERNAL_TEMPERATURE,
    CONF_TARGET_TEMPERATURE,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_MINUTE,
)

from . import ANOVA_NANO_CHILD_SCHEMA, CONF_ANOVA_NANO_ID

DEPENDENCIES = ["anova_nano"]

CONF_WATER_TEMPERATURE = "water_temperature"
CONF_HEATER_TEMPERATURE = "heater_temperature"
CONF_TRIAC_TEMPERATURE = "triac_temperature"
CONF_MOTOR_SPEED = "motor_speed"
CONF_TIMER = "timer"


def _temperature(category=cv.UNDEFINED):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=category,
    )


# All temperatures are published in °C; values reported in °F are converted.
SENSORS = {
    CONF_WATER_TEMPERATURE: _temperature(),
    CONF_TARGET_TEMPERATURE: _temperature(),
    CONF_HEATER_TEMPERATURE: _temperature(ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_TRIAC_TEMPERATURE: _temperature(ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_INTERNAL_TEMPERATURE: _temperature(ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_MOTOR_SPEED: sensor.sensor_schema(
        icon="mdi:fan",
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_TIMER: sensor.sensor_schema(
        unit_of_measurement=UNIT_MINUTE,
        icon="mdi:timer-outline",
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_DURATION,
    ),
}

CONFIG_SCHEMA = ANOVA_NANO_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ANOVA_NANO_ID])
    for key in SENSORS:
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
