import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import COMMAND_SCHEMA, HostCommand, configure_command, host_command_ns

HostCommandTextSensor = host_command_ns.class_(
    "HostCommandTextSensor", cg.PollingComponent, text_sensor.TextSensor, HostCommand
)

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(HostCommandTextSensor)
    .extend(COMMAND_SCHEMA)
    .extend(cv.polling_component_schema("60s"))
)

DEPENDENCIES = ["host"]


async def to_code(config: ConfigType) -> None:
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    configure_command(var, config)
