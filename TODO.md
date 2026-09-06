# TODOs from ChatGPT Web

## 3. Add numeric `sensor` support

Implement `sensor.py` and the corresponding C++ class.

Example target configuration:

```yaml
sensor:
  - platform: host_command
    name: "CPU Temperature"
    executable: /path/to/command
    arguments: []

    min_ok: 10.5
    max_ok: 80

    status:
      name: "CPU Temperature Status"
```

The command stdout is the numeric value.

Parsing requirements:

* Trim surrounding whitespace/newlines.
* Accept integers and floating point values.
* Accept either `.` or `,` as the decimal separator.
* Examples that must work:

  * `42`
  * `42.0`
  * `42,0`
  * `-3.25`
  * `-3,25`
* Require the entire trimmed stdout to represent one number.
* Do NOT accept partial numbers such as:

  * `42 C`
  * `42foo`
  * `42 43`
* Do not implement locale/thousands-separator parsing.
* Inputs containing both `.` and `,` should be rejected rather than guessed.
* Reject empty output.
* Reject NaN and positive/negative infinity.
* Publish the parsed value as ESPHome's normal floating-point sensor state.

If execution succeeds but parsing fails:

* keep the previous numeric sensor value
* status becomes Problem if configured
* log the parsing error

If the command itself fails:

* keep the previous numeric value
* status becomes Problem if configured

If a valid value is outside configured thresholds:

* publish the actual new value
* status becomes Problem

## 4. Optional numeric OK thresholds

Support:

```yaml
min_ok: <int or float>
max_ok: <int or float>
```

Both are optional and independently usable.

Semantics:

* value < min_ok -> Problem
* value > max_ok -> Problem
* equality to min/max is OK
* omitted minimum -> no lower-bound check
* omitted maximum -> no upper-bound check

Validate at configuration time that `min_ok <= max_ok` when both are provided.

## 5. Optional status binary sensor

For the numeric sensor, support:

```yaml
status:
  name: "CPU Temperature Status"
```

This should create an ESPHome binary sensor with `device_class: problem`.

Semantics:

* OFF = OK
* ON = Problem

`status:` must also be usable with no `min_ok`/`max_ok`. In that case it acts purely as a command/value health indicator:

* successful command + valid numeric value -> OK
* command failure -> Problem
* invalid numeric output -> Problem

For threshold-enabled sensors:

* successful + valid + in range -> OK
* successful + valid + outside range -> Problem
* execution failure -> Problem
* parse failure -> Problem

Keep the previous numeric value after execution or parsing failure.

## 6. Add button support

Implement the planned `button` platform using the same common runner.

A button executes a configured command when pressed.

Allow it optionally to reference an existing text sensor for execution status/result, using a clear option such as:

```yaml
text_sensor:
  - platform: template
    id: command_status
    name: "Command Status"

button:
  - platform: host_command
    name: "Run Command"
    executable: /path/to/program
    status_text_sensor: command_status
```

Use an ESPHome `text_sensor` ID reference rather than creating the status text sensor automatically.

Suggested status values should be short and useful, for example:

* `OK`
* `Exit 2`
* `Signal 9`
* `Exec failed`
* `Internal error`

Detailed errors should remain in the ESPHome log rather than making the HA state excessively long.

Button presses cannot themselves report a failed API button invocation to Home Assistant, so this status entity is the explicit mechanism for showing the result.

The button should also support stdin, arguments, allow_root, and the common logging options.

## 7. Logging

Keep the existing independently configurable logging:

```yaml
logging:
  arguments: true
  exit_status: true
  stderr: false
  stdin: false
  stdout: false
```

Refactor it so all command-based entity types can use the same implementation/settings.

Log useful distinctions:

* exited normally with code N
* terminated by signal N
* exec failed
* internal runner error
* numeric output parsing failed
* numeric value below/above configured OK threshold

Do not log stdout/stderr/stdin unless their respective logging options are enabled.

## 8. Root execution restriction

Preserve the current security behavior:

```yaml
allow_root: false
```

by default, refusing to execute commands when the ESPHome process itself is root unless an individual entity explicitly has:

```yaml
allow_root: true
```

Avoid duplicating this check independently in every entity if it can cleanly be part of shared host_command behavior.

## 9. Tests

Expand the repository tests substantially.

At minimum test configuration/code generation and, where practical, runner behavior for:

* text sensor existing configuration
* stdin
* stdout
* stderr
* exit 0
* non-zero exit
* exec failure
* SIGTERM/SIGKILL or another signal
* numeric integer
* numeric float with `.`
* numeric float with `,`
* negative numeric value
* malformed numeric output
* NaN/inf rejection
* min only
* max only
* min + max
* invalid min > max configuration
* optional status sensor
* status without thresholds
* button
* button with status text sensor

Preserve existing tests and compatibility.

## 10. README

After implementation, update README to describe what is actually implemented rather than leaving sensor/button/binary sensor as merely planned.

Add examples for:

* text sensor
* numeric sensor
* min/max/status
* button
* button status text sensor
* Bash explicitly used as executable with a multiline script supplied via stdin

Keep the warning that host_command does not invoke a shell itself.

Explicitly document that execution is currently synchronous/blocking and there is no timeout.

Update the installation/security example to use the intended defaults:

* service: `esphome-node`
* default installation directory: `/opt/esphome-node`
* default executable: `/opt/esphome-node/esphome-node`
* dedicated user/group: `esphome`

Do not claim features are implemented until the code/tests actually implement them.

Before finishing:

1. run formatting/linting appropriate for ESPHome
2. run the repository tests
3. compile/validate the test ESPHome YAML
4. show me a concise summary of changed files and any design decisions where you deviated from this TODO
5. do not commit or push unless I explicitly ask
