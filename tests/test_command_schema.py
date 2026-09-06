"""Compatibility and stdin validation for the shared command schema."""

import importlib
from pathlib import Path
from types import ModuleType

import pytest

import esphome.config_validation as cv
from esphome.core import CORE


@pytest.fixture
def command_module(monkeypatch: pytest.MonkeyPatch) -> ModuleType:
    """Load the external component using its own package directory."""
    CORE.reset()
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1] / "components"))
    return importlib.import_module("host_command.text_sensor")


def test_existing_defaults(command_module: ModuleType) -> None:
    """Existing YAML retains polling, argument, root, and logging defaults."""
    config = command_module.CONFIG_SCHEMA(
        {"name": "Test", "executable": "/usr/bin/date"}
    )
    assert config["update_interval"].total_milliseconds == 60000
    assert config["arguments"] == []
    assert config["allow_root"] is False
    assert config["stdin"] == ""
    assert config["logging"] == {
        "arguments": True,
        "exit_status": True,
        "stdin": False,
        "stdout": False,
        "stderr": False,
    }


def test_stdin_and_existing_options(command_module: ModuleType) -> None:
    """Input is preserved literally and independently of logging.stdin."""
    config = command_module.CONFIG_SCHEMA(
        {
            "name": "Test",
            "executable": "/usr/bin/cat",
            "arguments": "--",
            "stdin": "hello\n$HOME\r\n",
            "allow_root": True,
            "update_interval": "10s",
            "logging": {"arguments": False, "stdin": False},
        }
    )
    assert config["stdin"] == "hello\n$HOME\r\n"
    assert config["arguments"] == ["--"]
    assert config["allow_root"] is True
    assert config["update_interval"].total_milliseconds == 10000
    assert config["logging"]["arguments"] is False
    assert config["logging"]["stdin"] is False


@pytest.mark.parametrize("stdin", [False, 123, ["input"]])
def test_invalid_stdin(command_module: ModuleType, stdin: object) -> None:
    """Stdin must be a string, not a logging flag or argument list."""
    with pytest.raises(cv.Invalid):
        command_module.CONFIG_SCHEMA(
            {"name": "Test", "executable": "/usr/bin/cat", "stdin": stdin}
        )


def test_relative_executable(command_module: ModuleType) -> None:
    """Executable paths remain absolute."""
    with pytest.raises(cv.Invalid, match="absolute path"):
        command_module.CONFIG_SCHEMA({"name": "Test", "executable": "cat"})
