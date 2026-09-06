#pragma once

#include <string>
#include <vector>

namespace esphome::host_command {

struct CommandInvocation {
  std::string executable;
  std::vector<std::string> arguments;
  std::string stdin_data;
};

enum class CommandTermination {
  COMMAND_TERMINATION_EXITED,
  COMMAND_TERMINATION_SIGNALED,
  COMMAND_TERMINATION_EXEC_FAILED,
  COMMAND_TERMINATION_INTERNAL_ERROR,
};

struct CommandResult {
  std::string stdout_data;
  std::string stderr_data;
  CommandTermination termination{CommandTermination::COMMAND_TERMINATION_INTERNAL_ERROR};
  int exit_code{-1};
  int signal_number{0};
  std::string error;
};

// Blocking, without a timeout or shell. Output is returned verbatim. The caller
// enforces execution policy (including root opt-in). Stdin is closed after delivery.
CommandResult run_command(const CommandInvocation &invocation);

}  // namespace esphome::host_command
