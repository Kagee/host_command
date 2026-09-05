#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <string>
#include <vector>

namespace esphome {
namespace host_command {
class HostCommandTextSensor : public esphome::PollingComponent, public esphome::text_sensor::TextSensor {
 public:
  void setup() override;

  void update() override;

  void dump_config() override;

  void set_executable(const std::string &executable) { executable_ = executable; }

  void set_arguments(const std::vector<std::string> &arguments) { arguments_ = arguments; }

  void set_allow_root(bool allow_root) { allow_root_ = allow_root; }

  void set_log_arguments(bool value) { this->log_arguments_ = value; }
  void set_log_exit_status(bool value) { this->log_exit_status_ = value; }
  void set_log_stderr(bool value) { this->log_stderr_ = value; }
  void set_log_stdin(bool value) { this->log_stdin_ = value; }
  void set_log_stdout(bool value) { this->log_stdout_ = value; }

 protected:
  std::string executable_;
  std::vector<std::string> arguments_;

  bool allow_root_{false};

  bool log_arguments_{true};
  bool log_exit_status_{true};
  bool log_stderr_{false};
  bool log_stdin_{false};
  bool log_stdout_{false};
};
}  // namespace host_command
}  // namespace esphome
