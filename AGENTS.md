# Host Command development guide

## Scope and purpose

The subproject at `config/external_components/` is an independent Git repository
nested inside the ESPHome checkout. These instructions describe that subproject,
including when this file is read through a symlink at the checkout root.
Paths below are relative to the subproject unless explicitly marked as checkout paths.
This session and code tree are exclusively for the subproject. Treat the enclosing
ESPHome checkout as a build dependency and reference, not a development target.
Run Git commands from
`config/external_components/` so they target the correct repository. Do not create
commits or publish changes unless requested.

`host_command` is an external ESPHome component for the Linux `host` platform.
It executes commands configured in YAML and exposes their output to Home Assistant
through ESPHome text sensors. See `README.md` for usage and deployment details.

## Directory layout

- `components/host_command/__init__.py`: code-generation namespace.
- `components/host_command/text_sensor.py`: configuration schema and code generation.
- `components/host_command/host_command.h`: C++ class and configuration fields.
- `components/host_command/host_command.cpp`: execution, output capture, and publishing.
- `tests/test_host_command.yaml`: local component test; runs `date` every 10 seconds.
- `tests/device.yaml`: local device configuration, excluded from Git.
- `starter-components/`: Git-ignored reference templates, not the implementation.
- `logs/`: compilation output saved for inspection.

## Current behavior

- Only `text_sensor` is implemented. Numeric sensors, binary sensors, and buttons
  are planned features.
- Executables use absolute paths and predefined arguments. Execution uses `fork()`
  and `execv()` without an implicit shell.
- Stdout and stderr are captured separately. Trailing CR/LF characters are removed;
  stdout is published only when the command exits with status `0`.
- The default update interval is 60 seconds.
- Execution as root is refused unless the entity sets `allow_root: true`.
- Execution currently blocks the main loop and has no timeout.
- A `logging.stdin` setting exists, but supplying stdin is not implemented.

Keep this description and the README aligned when behavior changes. Preserve the
predefined-command model and root opt-in unless the requested change requires otherwise.

## Development conventions

The relevant ESPHome conventions are included below; this guide does not depend
on a separate parent `AGENTS.md`. Before component changes, consult the ESPHome
developer documentation, which takes precedence over these conventions if they disagree:

- https://developers.esphome.io/architecture/components/
- https://developers.esphome.io/architecture/components/advanced/

Keep changes focused, dependencies minimal, validation errors clear, and defaults
sensible. ESPHome parses YAML in Python and generates C++ for the target platform.
The parent checkout uses Python 3.12 or newer and C++ `gnu++20`. This component
targets Linux `host`; do not assume Arduino or ESP-IDF APIs are available.

Treat local device configurations as potentially containing credentials. Do not
copy their secrets into documentation, logs, or commits. Do not edit reference
templates or generated build files as part of ordinary component changes.

### Python, schemas, and code generation

- Follow PEP 8 and use descriptive `snake_case` names. Type-hint all function
  signatures, including tests, validators, and `to_code`. Import `ConfigType`
  from `esphome.types` for configuration dictionaries.
- Use the parent checkout's Ruff and Flake8 configuration in `pyproject.toml`.
- Prefer `if (value := config.get(CONF_KEY)) is not None:` when it avoids a
  repeated lookup. Do not use truthiness when `False`, zero, or an empty value
  is valid configuration.
- Reuse validators from `esphome/config_validation.py` before adding custom ones.
  Compose them with `cv.All` or `cv.Any`; use shared validators for ranges,
  platform restrictions, and mutually exclusive keys (`cv.has_at_most_one_key`
  or `cv.has_exactly_one_key`). Validate inputs before generating C++.
- Reuse existing `CONF_` constants. Define new constants locally in the component;
  keep shared constants within this subproject rather than changing ESPHome files.
- Declare the C++ namespace and class through `esphome.codegen`. Extend the
  appropriate entity schema and `cv.COMPONENT_SCHEMA` or
  `cv.polling_component_schema`. Create entities with the corresponding helper
  (such as `text_sensor.new_text_sensor`) and register components with
  `cg.register_component`.
- Pass required, invariant dependencies as constructor arguments through the
  creation helper or `cg.new_Pvariable`, rather than initializing them later with
  setters. Use setters for optional or changeable properties.
- Declare component metadata as needed: `DEPENDENCIES`, `AUTO_LOAD`,
  `CONFLICTS_WITH`, `CODEOWNERS`, and `MULTI_CONF`.
- Store per-configuration mutable state under `CORE.data[DOMAIN]`, preferably
  using a dataclass or a typed dictionary. Do not use mutable module globals:
  they survive between configuration runs, whereas `CORE.data` is cleared.
- Use `cg.add_library` for C++ libraries and `cg.add_build_flag` for compiler
  flags. Keep dependency changes within this external component's scope.

### C++ conventions and component lifecycle

- Use the `esphome::host_command` namespace and the parent `.clang-format`.
  Indent with two spaces and keep lines at most 120 characters long.
- Use `lower_snake_case` for methods and variables, `UpperCamelCase` for types,
  `UPPER_SNAKE_CASE` for namespace constants, and `lower_snake_case` for local
  constants. Member fields have a trailing underscore; prefix member access
  with `this->`. Prefer `using` over `typedef`.
- Prefix enum-class values with the enum name in `UPPER_SNAKE_CASE`, such as
  `CommandResult::COMMAND_RESULT_SUCCESS`, to avoid platform macro collisions.
- Prefer `protected` fields for extensibility. Use `private` when pointer
  lifetimes, coupled invariants, or resource cleanup require controlled access;
  provide protected accessors when derived classes need them.
- Use constants or enums instead of preprocessor macros for values. Reserve
  macros for conditional compilation and code-generated compile-time sizes.
- Do not override a base method just to return its default. In particular,
  `Component::get_setup_priority()` already returns `setup_priority::DATA`.
- In `loop()`, use `App.get_loop_component_start_time()` from
  `esphome/core/application.h`. Only use `millis()` when a long operation needs
  sub-tick resolution. The normal loop cadence is about 16 ms, so shorter
  rate-limit gates are ineffective. Use a gated `loop()` for cadences under
  250 ms and `set_interval` for 500 ms or more; consult the advanced lifecycle
  documentation when selecting scheduling and deferred-work primitives.
- Wrap string literals passed as `%s` logging arguments in `LOG_STR_LITERAL()`.

### Memory and callbacks

- Avoid heap allocations after `setup()` unless unavoidable. Reuse buffers and
  prefer views over repeated string copies. The Linux target has more memory
  than a microcontroller, but the component should still follow ESPHome's
  allocation-conscious conventions.
- Use `std::array` for compile-time fixed sizes, `StaticVector` from
  `esphome/core/helpers.h` for fixed capacity with `push_back`, and `FixedVector`
  for sizes determined during runtime initialization. Use a growing vector only
  when growth is needed. Fixed byte buffers can use `std::array` or
  `std::unique_ptr<uint8_t[]>`; the latter provides neither bounds checks nor
  container iterators.
- For child or listener counts known from configuration, use `cg.slot_counter`
  and a generated define to size storage and compile it out when unused. Request
  slots from `to_code`, before the final code-generation phase.
- Prefer simple arrays or vectors with linear lookup for small datasets over
  maps or hash tables unless profiling justifies the latter. Avoid `std::deque`
  and its block-allocation overhead.
- Use `StringRef` from `esphome/core/string_ref.h` for configuration strings
  whose storage outlives the component. It is non-owning; do not use it for
  temporary strings or captured process output whose buffer will be released.
- Prefer `LazyCallbackManager<void(Ts...)>` when callbacks often have no
  subscribers; use `CallbackManager` when subscribers are normally present.
  Registration methods must accept a template callable and forward it with
  `std::forward<F>(callback)` instead of forcing a `std::function` conversion.
- Use `automation.build_callback_automation` for simple triggers, with
  `automation.validate_automation({})` and no `CONF_TRIGGER_ID`. Boolean filters
  can use `TriggerOnTrueForwarder` or `TriggerOnFalseForwarder` with no arguments.
  Use a C++ `Trigger<Ts...>` subclass and `build_automation` only when mutable
  state beyond the automation pointer is needed, such as edge or timing state.
- Register actions with `automation.register_action`. Set `synchronous=True`
  only when `play()` completes without suspension; use `False` for deferred
  execution or stored trigger arguments. Register conditions with
  `automation.register_condition` and implement `check()`.

### Compatibility and documentation

- Prefer documented ESPHome APIs. Public core/base-class C++ members are public
  API; undocumented component members generally are not. Public members of
  components with global accessors are public API except configuration setters.
  Python interfaces are internal unless documented for external components or
  part of the core API actively used by core components.
- Preserve existing YAML behavior where possible. Explain breaking changes and
  provide migration examples in the README. Test compatibility whenever the
  component continues to support an old interface.
- Rename configuration keys with `cv.rename_key(..., removed_in=...,
  component="host_command")` so validation warns and migrates them. Use
  `ESPDEPRECATED` for deprecated C++ APIs when appropriate.
- Write documentation and comments in plain English. Keep
  line comments short and explain non-obvious behavior rather than restating
  code. Document function contracts and parameters concisely.

## Validation and linting

Run development tools using `python3 script/run-in-env.py` from the parent
checkout root. For configuration-only validation:

```sh
python3 script/run-in-env.py esphome config config/external_components/tests/test_host_command.yaml
```

Use relevant `pytest` tests for Python behavior, `clang-tidy` for C++ analysis,
and the parent's `prek` hooks for changed source files when applicable. Keep
checks scoped to the affected behavior and supported Linux host platform.
Schema-only changes can be checked with `esphome config`; C++ changes need
compilation as described below. Documentation-only edits do not need firmware builds.

## Compilation

Run from the **parent ESPHome checkout root** (the directory containing `script/`
and `config/`). The environment helper activates the checkout's virtual environment;
the bare `esphome` command may not be available on the shell's PATH.

```sh
python3 script/run-in-env.py esphome compile config/external_components/tests/test_host_command.yaml
```

Create log files only when compiling. Save compilation stdout and stderr to a new
timestamped file in this project's `logs/` directory. Do not create log files for
inspection, editing, validation, or other non-compilation commands. For compilation, use:

```sh
mkdir -p config/external_components/logs
compile_log="config/external_components/logs/compile-$(date +%Y%m%d-%H%M%S-%N).log"
python3 script/run-in-env.py esphome compile config/external_components/tests/test_host_command.yaml > "$compile_log" 2>&1
compile_status=$?
printf 'Log: %s\nExit status: %s\n' "$compile_log" "$compile_status"
```

Check `compile_status` and inspect the log before reporting success. If wrapping
this in a standalone script, end it with `exit "$compile_status"` to preserve failure
status. Link to the log in the response and summarize the result rather than
pasting the entire output. Keep generated logs out of commits.

Compilation may report that the program is already up to date. State that clearly;
do not describe it as a clean rebuild. Compilation does not run the program or
verify runtime behavior. Do not launch the host application or execute its configured
commands unless requested.

For changes, run checks appropriate to the affected behavior and report what was
actually verified, including any limitations. Do not claim tests passed if they
were not run.
