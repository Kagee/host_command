#include "host_command.h"

#include "esphome/core/log.h"
#include <unistd.h>

namespace esphome::host_command {

static const char *const TAG = "host_command";

static void trim_line_endings(std::string &data) {
  while (!data.empty() && (data.back() == '\r' || data.back() == '\n'))
    data.pop_back();
}

CommandResult HostCommand::execute_command_() {
  if (geteuid() == 0 && !this->allow_root_) {
    CommandResult result;
    result.error = "Execution as root requires allow_root: true";
    ESP_LOGE(TAG, "Refusing to execute '%s' as root; set allow_root: true to permit this command",
             this->invocation_.executable.c_str());
    return result;
  }
  if (this->log_arguments_) {
    ESP_LOGD(TAG, "Executable: %s", this->invocation_.executable.c_str());
    for (size_t i = 0; i < this->invocation_.arguments.size(); i++)
      ESP_LOGD(TAG, "Argument %zu: %s", i, this->invocation_.arguments[i].c_str());
  }
  if (this->log_stdin_)
    ESP_LOGD(TAG, "stdin: %s", this->invocation_.stdin_data.c_str());

  auto result = run_command(this->invocation_);
  trim_line_endings(result.stdout_data);
  trim_line_endings(result.stderr_data);
  if (this->log_stdout_)
    ESP_LOGD(TAG, "stdout: %s", result.stdout_data.c_str());
  if (this->log_stderr_)
    ESP_LOGD(TAG, "stderr: %s", result.stderr_data.c_str());
  if (!result.error.empty())
    ESP_LOGE(TAG, "Command '%s' failed: %s", this->invocation_.executable.c_str(), result.error.c_str());
  if (this->log_exit_status_) {
    if (result.termination == esphome::host_command::CommandTermination::COMMAND_TERMINATION_EXITED) {
      ESP_LOGD(TAG, "Exited with status %d", result.exit_code);
    } else if (result.termination == CommandTermination::COMMAND_TERMINATION_SIGNALED) {
      ESP_LOGD(TAG, "Terminated by signal %d", result.signal_number);
    }
  }
  return result;
}

void HostCommandTextSensor::update() {
  const auto result = this->execute_command_();
  if (result.termination == CommandTermination::COMMAND_TERMINATION_EXITED && result.exit_code == 0)
    this->publish_state(result.stdout_data);
}

void HostCommandTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Host Command Text Sensor", this);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->get_update_interval());
  this->dump_command_config_();
}

void HostCommand::setup_command_() {
  if (geteuid() == 0 && !this->allow_root_) {
    ESP_LOGW(TAG,
             "ESPHome is running as root; '%s' will not be executed "
             "because allow_root is false",
             this->invocation_.executable.c_str());
  }
}

void HostCommand::dump_command_config_() {
  ESP_LOGCONFIG(TAG, "  Executable: %s", this->invocation_.executable.c_str());

  ESP_LOGCONFIG(TAG, "  Arguments: %zu", this->invocation_.arguments.size());

  // We may not want this if arguments contains sensitive information.
  if (this->log_arguments_) {
    for (const auto &arg : this->invocation_.arguments) {
      ESP_LOGCONFIG(TAG, "    %s", arg.c_str());
    }
  } else {
    ESP_LOGCONFIG(TAG, "    [redacted, use logging.arguments: true to view]");
  }

  ESP_LOGCONFIG(TAG, "  Allow root: %s", YESNO(this->allow_root_));

  ESP_LOGCONFIG(TAG, "  Logging:");
  ESP_LOGCONFIG(TAG, "    Arguments: %s", YESNO(this->log_arguments_));
  ESP_LOGCONFIG(TAG, "    Exit status: %s", YESNO(this->log_exit_status_));
  ESP_LOGCONFIG(TAG, "    Stderr: %s", YESNO(this->log_stderr_));
  ESP_LOGCONFIG(TAG, "    Stdin: %s", YESNO(this->log_stdin_));
  ESP_LOGCONFIG(TAG, "    Stdout: %s", YESNO(this->log_stdout_));
}
}  // namespace esphome::host_command
