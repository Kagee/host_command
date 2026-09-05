import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

from . import host_command_ns

CONF_EXECUTABLE = "executable"
CONF_ARGUMENTS = "arguments"
CONF_ALLOW_ROOT = "allow_root"

CONF_LOGGING = "logging"

CONF_ARGUMENTS = "arguments"
CONF_EXIT_STATUS = "exit_status"
CONF_STDERR = "stderr"
CONF_STDIN = "stdin"
CONF_STDOUT = "stdout"

HostCommandTextSensor = host_command_ns.class_(
    "HostCommandTextSensor",
    cg.PollingComponent,
    text_sensor.TextSensor,
)

def absolute_path(value):
    value = cv.string_strict(value)

    if not value.startswith("/"):
        raise cv.Invalid("executable must be an absolute path")

    return value

LOGGING_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ARGUMENTS, default=True): cv.boolean,
        cv.Optional(CONF_EXIT_STATUS, default=True): cv.boolean,
        cv.Optional(CONF_STDERR, default=False): cv.boolean,
        cv.Optional(CONF_STDIN, default=False): cv.boolean,
        cv.Optional(CONF_STDOUT, default=False): cv.boolean,
    }
)

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(HostCommandTextSensor)
    .extend(
        {
            cv.Required(CONF_EXECUTABLE): absolute_path,
            cv.Optional(CONF_ARGUMENTS, default=[]): cv.ensure_list(
                cv.string_strict
            ),
            cv.Optional(CONF_ALLOW_ROOT, default=False): cv.boolean,
            cv.Optional(CONF_LOGGING, default={}): LOGGING_SCHEMA,
        }
    )
    .extend(cv.polling_component_schema("60s"))
)

DEPENDENCIES = ["host"]

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)

    cg.add(var.set_executable(config[CONF_EXECUTABLE]))
    cg.add(var.set_arguments(config[CONF_ARGUMENTS]))
    cg.add(var.set_allow_root(config[CONF_ALLOW_ROOT]))

    logging = config[CONF_LOGGING]

    cg.add(var.set_log_arguments(logging[CONF_ARGUMENTS]))
    cg.add(var.set_log_exit_status(logging[CONF_EXIT_STATUS]))
    cg.add(var.set_log_stderr(logging[CONF_STDERR]))
    cg.add(var.set_log_stdin(logging[CONF_STDIN]))
    cg.add(var.set_log_stdout(logging[CONF_STDOUT]))
