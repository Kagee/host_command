#include "host_command.h"

#include "esphome/core/log.h"

#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace esphome {
namespace host_command {

static const char *const TAG = "host_command";

void HostCommandTextSensor::update() {
  if (geteuid() == 0 && !this->allow_root_) {
    ESP_LOGE(TAG,
             "Refusing to execute '%s' as root; "
             "set allow_root: true to permit this command",
             this->executable_.c_str());
    return;
  }

  if (this->log_arguments_) {
    ESP_LOGD(TAG, "Executable: %s", this->executable_.c_str());

    for (size_t i = 0; i < this->arguments_.size(); i++) {
      ESP_LOGD(TAG, "Argument %zu: %s", i, this->arguments_[i].c_str());
    }
  }

  int stdout_pipe[2];
  int stderr_pipe[2];

  if (pipe(stdout_pipe) != 0) {
    ESP_LOGE(TAG, "Failed to create stdout pipe: %s", strerror(errno));
    return;
  }

  if (pipe(stderr_pipe) != 0) {
    ESP_LOGE(TAG, "Failed to create stderr pipe: %s", strerror(errno));
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    return;
  }

  pid_t pid = fork();

  if (pid < 0) {
    ESP_LOGE(TAG, "fork() failed: %s", strerror(errno));

    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    return;
  }

  if (pid == 0) {
    // Child process

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }

    if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
      _exit(126);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(this->arguments_.size() + 2);

    argv.push_back(const_cast<char *>(this->executable_.c_str()));

    for (auto &argument : this->arguments_) {
      argv.push_back(const_cast<char *>(argument.c_str()));
    }

    argv.push_back(nullptr);

    execv(this->executable_.c_str(), argv.data());

    // execv() only returns on failure.
    _exit(127);
  }

  // Parent process

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  std::string stdout_output;
  std::string stderr_output;

  struct pollfd fds[2] = {
      {
          .fd = stdout_pipe[0],
          .events = POLLIN,
          .revents = 0,
      },
      {
          .fd = stderr_pipe[0],
          .events = POLLIN,
          .revents = 0,
      },
  };

  bool stdout_open = true;
  bool stderr_open = true;

  while (stdout_open || stderr_open) {
    int result = poll(fds, 2, -1);

    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }

      ESP_LOGE(TAG, "poll() failed: %s", strerror(errno));
      break;
    }

    for (int i = 0; i < 2; i++) {
      if (fds[i].fd < 0) {
        continue;
      }

      if (fds[i].revents & POLLIN) {
        char buffer[512];
        ssize_t count = read(fds[i].fd, buffer, sizeof(buffer));

        if (count > 0) {
          if (i == 0) {
            stdout_output.append(buffer, count);
          } else {
            stderr_output.append(buffer, count);
          }
        } else if (count == 0) {
          close(fds[i].fd);
          fds[i].fd = -1;

          if (i == 0) {
            stdout_open = false;
          } else {
            stderr_open = false;
          }
        } else if (errno != EINTR) {
          ESP_LOGE(TAG, "read() failed: %s", strerror(errno));

          close(fds[i].fd);
          fds[i].fd = -1;

          if (i == 0) {
            stdout_open = false;
          } else {
            stderr_open = false;
          }
        }
      }

      if (fds[i].fd >= 0 &&
          (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))) {
        char buffer[512];

        while (true) {
          ssize_t count = read(fds[i].fd, buffer, sizeof(buffer));

          if (count > 0) {
            if (i == 0) {
              stdout_output.append(buffer, count);
            } else {
              stderr_output.append(buffer, count);
            }

            continue;
          }

          break;
        }

        close(fds[i].fd);
        fds[i].fd = -1;

        if (i == 0) {
          stdout_open = false;
        } else {
          stderr_open = false;
        }
      }
    }
  }

  // Make sure both descriptors are closed if we left the loop because of
  // an error.
  if (fds[0].fd >= 0) {
    close(fds[0].fd);
  }

  if (fds[1].fd >= 0) {
    close(fds[1].fd);
  }

  int status = 0;

  if (waitpid(pid, &status, 0) < 0) {
    ESP_LOGE(TAG, "waitpid() failed: %s", strerror(errno));
    return;
  }

  while (!stdout_output.empty() &&
         (stdout_output.back() == '\n' || stdout_output.back() == '\r')) {
    stdout_output.pop_back();
  }

  while (!stderr_output.empty() &&
         (stderr_output.back() == '\n' || stderr_output.back() == '\r')) {
    stderr_output.pop_back();
  }

  if (this->log_stdout_) {
    ESP_LOGD(TAG, "stdout: %s", stdout_output.c_str());
  }

  if (this->log_stderr_) {
    ESP_LOGD(TAG, "stderr: %s", stderr_output.c_str());
  }

  if (WIFEXITED(status)) {
    const int exit_code = WEXITSTATUS(status);

    if (this->log_exit_status_) {
      ESP_LOGD(TAG, "Exited with status %d", exit_code);
    }

    if (exit_code != 0) {
      return;
    }

    this->publish_state(stdout_output);
    return;
  }

  if (WIFSIGNALED(status)) {
    if (this->log_exit_status_) {
      ESP_LOGD(TAG, "Terminated by signal %d", WTERMSIG(status));
    }

    return;
  }

  if (this->log_exit_status_) {
    ESP_LOGD(TAG, "Process ended with unhandled wait status 0x%x", status);
  }
}

void HostCommandTextSensor::setup() {
  if (geteuid() == 0 && !this->allow_root_) {
    ESP_LOGW(TAG,
             "ESPHome is running as root; '%s' will not be executed "
             "because allow_root is false",
             this->executable_.c_str());
  }
}

void HostCommandTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Host Command Text Sensor", this);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->get_update_interval());

  ESP_LOGCONFIG(TAG, "  Executable: %s", this->executable_.c_str());

  ESP_LOGCONFIG(TAG, "  Arguments: %zu", this->arguments_.size());

  // We may not want this if arguments contains sensitive information.
  if (this->log_arguments_) {
    for (const auto &arg : this->arguments_) {
      ESP_LOGCONFIG(TAG, "    %s", arg.c_str());
    }
  } else {
    ESP_LOGCONFIG(TAG, "    [redacted, use log_arguments: true to view]");
  }

  ESP_LOGCONFIG(TAG, "  Allow root: %s", YESNO(this->allow_root_));

  ESP_LOGCONFIG(TAG, "  Logging:");
  ESP_LOGCONFIG(TAG, "    Arguments: %s", YESNO(this->log_arguments_));
  ESP_LOGCONFIG(TAG, "    Exit status: %s", YESNO(this->log_exit_status_));
  ESP_LOGCONFIG(TAG, "    Stderr: %s", YESNO(this->log_stderr_));
  ESP_LOGCONFIG(TAG, "    Stdin: %s", YESNO(this->log_stdin_));
  ESP_LOGCONFIG(TAG, "    Stdout: %s", YESNO(this->log_stdout_));
}
}  // namespace host_command
}  // namespace esphome