import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_DURATION, UNIT_MINUTE

from . import ANOVA_NANO_CHILD_SCHEMA, CONF_ANOVA_NANO_ID, anova_nano_ns

DEPENDENCIES = ["anova_nano"]

CONF_TIMER = "timer"

AnovaNanoTimerNumber = anova_nano_ns.class_(
    "AnovaNanoTimerNumber", number.Number, cg.Component
)

# The device stores minutes in an int32; 99 h 59 min matches the cooker's own display limit.
TIMER_MAX_MINUTES = 5999

CONFIG_SCHEMA = ANOVA_NANO_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_TIMER): number.number_schema(
            AnovaNanoTimerNumber,
            icon="mdi:timer-cog-outline",
            device_class=DEVICE_CLASS_DURATION,
            unit_of_measurement=UNIT_MINUTE,
        ).extend(cv.COMPONENT_SCHEMA),
    }
)


async def to_code(config):
    if conf := config.get(CONF_TIMER):
        var = await number.new_number(
            conf, min_value=0, max_value=TIMER_MAX_MINUTES, step=1
        )
        await cg.register_component(var, conf)
        await cg.register_parented(var, config[CONF_ANOVA_NANO_ID])
