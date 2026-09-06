"""Exercise the real Linux runner with an isolated test child, not the host node."""

from datetime import UTC, datetime
from pathlib import Path
import subprocess


def test_command_runner(tmp_path: Path) -> None:
    """Compile and run process/pipe regression cases with a test-only deadline."""
    project = Path(__file__).resolve().parents[1]
    component = project / "components" / "host_command"
    executable = tmp_path / "command_runner_test"
    logs = project / "logs"
    logs.mkdir(exist_ok=True)
    log = logs / f"compile-runner-{datetime.now(UTC):%Y%m%d-%H%M%S-%f}.log"
    with log.open("w") as output:
        compiled = subprocess.run(
            [
                "g++",
                "-std=gnu++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pthread",
                "-Wl,--wrap=poll,--wrap=read,--wrap=fork,--wrap=dup2,--wrap=waitpid",
                "-I",
                str(component),
                str(component / "command_runner.cpp"),
                str(project / "tests" / "command_runner_test.cpp"),
                "-o",
                str(executable),
            ],
            stdout=output,
            stderr=subprocess.STDOUT,
            check=False,
        )
    assert compiled.returncode == 0, log.read_text()
    subprocess.run([str(executable)], check=True, timeout=20)
