import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv

from . import ANOVA_NANO_CHILD_SCHEMA, CONF_ANOVA_NANO_ID, anova_nano_ns

DEPENDENCIES = ["anova_nano"]

AnovaNanoClimate = anova_nano_ns.class_(
    "AnovaNanoClimate", climate.Climate, cg.Component
)

CONFIG_SCHEMA = (
    climate.climate_schema(AnovaNanoClimate)
    .extend(ANOVA_NANO_CHILD_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ANOVA_NANO_ID])
