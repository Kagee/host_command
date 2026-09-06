#pragma once

#include "command_runner.h"
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <string>
#include <vector>

namespace esphome::host_command {
// Shared configuration, execution policy, and logging for command entities.
class HostCommand {
 public:
  void set_executable(const std::string &executable) { this->invocation_.executable = executable; }

  void set_arguments(const std::vector<std::string> &arguments) { this->invocation_.arguments = arguments; }

  void set_stdin(const std::string &data) { this->invocation_.stdin_data = data; }

  void set_allow_root(bool allow_root) { this->allow_root_ = allow_root; }

  void set_log_arguments(bool value) { this->log_arguments_ = value; }
  void set_log_exit_status(bool value) { this->log_exit_status_ = value; }
  void set_log_stderr(bool value) { this->log_stderr_ = value; }
  void set_log_stdin(bool value) { this->log_stdin_ = value; }
  void set_log_stdout(bool value) { this->log_stdout_ = value; }

 protected:
  void setup_command_();
  void dump_command_config_();
  CommandResult execute_command_();

  CommandInvocation invocation_;

  bool allow_root_{false};

  bool log_arguments_{true};
  bool log_exit_status_{true};
  bool log_stderr_{false};
  bool log_stdin_{false};
  bool log_stdout_{false};
};
class HostCommandTextSensor : public PollingComponent, public text_sensor::TextSensor, public HostCommand {
 public:
  void setup() override { this->setup_command_(); }
  void update() override;
  void dump_config() override;
};
}  // namespace esphome::host_command
