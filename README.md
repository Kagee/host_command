# ESPHome Host Command

`host_command` is an ESPHome component for the `host` platform that executes predefined local commands and exposes their results through ESPHome entities.

This component is currently under __early__ development.

The component is intended for running ESPHome directly on a Linux host and integrating local system information and administrative operations with Home Assistant.

## Development disclaimer

This component was primarily designed and written with the assistance of **ChatGPT (GPT-5.6 Sol)**. The generated code and design decisions have been manually reviewed, compiled, and tested by the project author.

The author is developing an ESPHome component for the first time and had not written C or C++ for approximately 15 years before starting this project. As a result, the code should not be assumed to follow ESPHome or modern C++ best practices simply because it compiles and works. Additional review, testing, and contributions from experienced ESPHome and C++ developers are welcome.

## Features

Planned entity support:

* `text_sensor` — publish command stdout as text.
* `sensor` — parse command stdout as a numeric value.
* `binary_sensor` — derive state from command execution results.
* `button` — execute a predefined command.

Commands are executed directly using `fork()` and `execv()`.

**No shell is invoked by `host_command`.**

This means shell functionality such as the following is not interpreted:

* Pipes (`|`)
* Redirection (`>`, `<`)
* Command substitution (`$()`)
* Environment expansion (`$VAR`)
* Globbing (`*`)
* Command chaining (`&&`, `;`)

If shell functionality is required, create a script containing the required logic and configure `host_command` to execute that script directly.

## Example

```yaml
external_components:
  - source:
      type: local
      path: external_components

host:

text_sensor:
  - platform: host_command
    name: "ZFS Pool Status"
    executable: /usr/sbin/zpool
    arguments:
      - list
      - -H
      - -o
      - health
      - tank
    update_interval: 30min
    logging:
      arguments: true
      exit_status: true
      stderr: false
      stdin: false
      stdout: false
```

The equivalent command is:

```text
/usr/sbin/zpool list -H -o health tank
```

but it is executed directly rather than through a shell.

## Logging

Logging can be controlled independently for different parts of command execution:

```yaml
logging:
  arguments: true
  exit_status: true
  stderr: false
  stdin: false
  stdout: false
```

Defaults:

| Setting       | Default | Description                         |
| ------------- | ------- | ----------------------------------- |
| `arguments`   | `true`  | Log command arguments               |
| `exit_status` | `true`  | Log process exit/termination status |
| `stderr`      | `false` | Log captured standard error         |
| `stdin`       | `false` | Log configured standard input       |
| `stdout`      | `false` | Log captured standard output        |

Command arguments, stdin, stdout, and stderr may contain sensitive information. Enable the corresponding logging options only when appropriate.

## Security

`host_command` deliberately does not provide arbitrary remote command execution.

The executable absolute path, arguments, and any configured stdin are defined in the ESPHome configuration and compiled into the ESPHome host executable. Home Assistant/API clients cannot provide arbitrary commands or arguments at runtime.

Commands should normally run as a dedicated, unprivileged Unix user such as `esphome`.

By default, commands are not allowed to execute when the ESPHome process itself is running as root. Individual commands can explicitly opt in:

```yaml
allow_root: true
```

This is intentionally configured per entity rather than globally.

### Protecting the ESPHome executable

Configured commands may contain sensitive information in their executable arguments or stdin. Using ESPHome `!secret` prevents values from appearing directly in the YAML configuration, but does not guarantee that those values cannot be recovered from the compiled executable. **The compiled executable may also contain other ESPHome credentials, including the native API encryption key and OTA password.**

The ESPHome host executable should therefore not be readable by unrelated local users.

For an OTA-enabled installation, a suitable layout is:

```text
/opt/esphome-node/
└── node (binary)
```

with the directory and executable owned by the dedicated ESPHome account:

```bash
chown -R esphome:esphome /opt/esphome-node
chmod 700 /opt/esphome-node
chmod 700 /opt/esphome-node/node
```

This allows the ESPHome process to replace its executable during OTA while preventing unrelated local users (apart from `root` and `esphome`) from reading or modifying it.

### OTA security

ESPHome host OTA replaces the running ESPHome executable.

Anyone able to perform an authenticated OTA update can therefore execute arbitrary native code with the privileges of the Unix account running the ESPHome process.

OTA credentials should consequently be treated as credentials granting code execution as that Unix user.

Running ESPHome as an unprivileged dedicated account limits the impact of such access.

## Development

A convenient source layout is:

```text
external_components/
├── test_host_command.yaml
└── host_command/
    ├── __init__.py
    ├── text_sensor.py
    ├── host_command.cpp
    └── host_command.h
```

The test configuration can reference the component directly:

```yaml
external_components:
  - source:
      type: local
      path: .
```

Validate the configuration with:

```bash
esphome config external_components/test_host_command.yaml
```

Compile it with:

```bash
esphome compile external_components/test_host_command.yaml
```

## Status

This component is currently under __early__ development.

The initial implementation focuses on `text_sensor` command execution and stdout capture.

Planned work includes:

* Shared command execution code for all entity types.
* Non-blocking command execution.
* Timeout handling.
* Standard input support.
* Separate stdout and stderr capture.
* Numeric sensors.
* Binary sensors.
* Buttons.
