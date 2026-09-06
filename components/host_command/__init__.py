import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.types import ConfigType

host_command_ns = cg.esphome_ns.namespace("host_command")
HostCommand = host_command_ns.class_("HostCommand")

CONF_EXECUTABLE = "executable"
CONF_ARGUMENTS = "arguments"
CONF_ALLOW_ROOT = "allow_root"

CONF_LOGGING = "logging"

CONF_EXIT_STATUS = "exit_status"
CONF_STDERR = "stderr"
CONF_STDIN = "stdin"
CONF_STDOUT = "stdout"


def absolute_path(value: str) -> str:
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

COMMAND_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_EXECUTABLE): absolute_path,
        cv.Optional(CONF_ARGUMENTS, default=[]): cv.ensure_list(cv.string_strict),
        cv.Optional(CONF_STDIN, default=""): cv.string_strict,
        cv.Optional(CONF_ALLOW_ROOT, default=False): cv.boolean,
        cv.Optional(CONF_LOGGING, default={}): LOGGING_SCHEMA,
    }
)


def configure_command(var: cg.MockObj, config: ConfigType) -> None:
    cg.add(var.set_executable(config[CONF_EXECUTABLE]))
    cg.add(var.set_arguments(config[CONF_ARGUMENTS]))
    cg.add(var.set_allow_root(config[CONF_ALLOW_ROOT]))

    logging = config[CONF_LOGGING]

    cg.add(var.set_log_arguments(logging[CONF_ARGUMENTS]))
    cg.add(var.set_log_exit_status(logging[CONF_EXIT_STATUS]))
    cg.add(var.set_log_stderr(logging[CONF_STDERR]))
    cg.add(var.set_log_stdin(logging[CONF_STDIN]))
    cg.add(var.set_log_stdout(logging[CONF_STDOUT]))

    cg.add(var.set_stdin(config[CONF_STDIN]))
