import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import ANOVA_NANO_CHILD_SCHEMA, CONF_ANOVA_NANO_ID, anova_nano_ns

DEPENDENCIES = ["anova_nano"]

CONF_UNIT = "unit"

AnovaNanoUnitSelect = anova_nano_ns.class_(
    "AnovaNanoUnitSelect", select.Select, cg.Component
)

# Index order is relied on by AnovaNanoUnitSelect (0 = C, 1 = F).
UNIT_OPTIONS = ["C", "F"]

CONFIG_SCHEMA = ANOVA_NANO_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_UNIT): select.select_schema(
            AnovaNanoUnitSelect,
            icon="mdi:thermometer",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ).extend(cv.COMPONENT_SCHEMA),
    }
)


async def to_code(config):
    if conf := config.get(CONF_UNIT):
        var = await select.new_select(conf, options=UNIT_OPTIONS)
        await cg.register_component(var, conf)
        await cg.register_parented(var, config[CONF_ANOVA_NANO_ID])
