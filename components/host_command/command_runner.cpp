#include "command_runner.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>

namespace esphome::host_command {
namespace {

class Pipe {
 public:
  ~Pipe() {
    this->close_end(0);
    this->close_end(1);
  }
  bool open() {
    if (pipe2(this->fds_.data(), O_CLOEXEC) < 0)
      return false;
    // Keep pipe descriptors clear of dup2 targets even if host stdio is closed.
    for (int &fd : this->fds_) {
      if (fd > STDERR_FILENO)
        continue;
      int replacement = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
      if (replacement < 0)
        return false;
      close(fd);
      fd = replacement;
    }
    return true;
  }
  int operator[](size_t index) const { return this->fds_[index]; }
  void close_end(size_t index) {
    if (this->fds_[index] >= 0) {
      close(this->fds_[index]);
      this->fds_[index] = -1;
    }
  }

 private:
  std::array<int, 2> fds_{-1, -1};
};

struct ChildError {
  int exec_failed;
  int error_number;
};

// Only async-signal-safe operations are used between fork and exec.
[[noreturn]] void child_error(int fd, bool exec_failed) {
  ChildError error{exec_failed ? 1 : 0, errno};
  const char *data = reinterpret_cast<const char *>(&error);
  size_t offset = 0;
  while (offset < sizeof(error)) {
    ssize_t count = write(fd, data + offset, sizeof(error) - offset);
    if (count > 0) {
      offset += count;
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  _exit(127);
}

std::string system_error(const char *operation, int error_number) {
  return std::string(operation) + ": " + strerror(error_number);
}

// Block SIGPIPE only in this thread, without changing the host's signal handler.
class SigpipeGuard {
 public:
  int block() {
    sigemptyset(&this->mask_);
    sigaddset(&this->mask_, SIGPIPE);
    int error = pthread_sigmask(SIG_BLOCK, &this->mask_, &this->previous_);
    if (error != 0)
      return error;
    this->active_ = true;
    sigset_t pending;
    if (sigpending(&pending) < 0)
      return errno;
    this->was_pending_ = sigismember(&pending, SIGPIPE) == 1;
    return 0;
  }
  void consume() {
    if (this->was_pending_)
      return;
    const timespec no_wait{0, 0};
    while (sigtimedwait(&this->mask_, nullptr, &no_wait) < 0 && errno == EINTR) {
    }
  }
  ~SigpipeGuard() {
    if (this->active_)
      pthread_sigmask(SIG_SETMASK, &this->previous_, nullptr);
  }

 private:
  sigset_t mask_{};
  sigset_t previous_{};
  bool active_{false};
  bool was_pending_{false};
};

}  // namespace

CommandResult run_command(const CommandInvocation &invocation) {
  CommandResult result;
  // Build argv before fork; the child must not allocate memory.
  std::vector<char *> argv(invocation.arguments.size() + 2);
  argv[0] = const_cast<char *>(invocation.executable.c_str());
  for (size_t i = 0; i < invocation.arguments.size(); i++)
    argv[i + 1] = const_cast<char *>(invocation.arguments[i].c_str());
  argv.back() = nullptr;

  Pipe input, output, errors, exec_errors;
  if (!input.open() || !output.open() || !errors.open() || !exec_errors.open()) {
    result.error = system_error("pipe creation", errno);
    return result;
  }
  // Only the parent's stdin writer needs nonblocking mode. Poll services all
  // three data streams concurrently so large input/output cannot deadlock.
  if (fcntl(input[1], F_SETFL, O_NONBLOCK) < 0) {
    result.error = system_error("fcntl(stdin)", errno);
    return result;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    result.error = system_error("fork", errno);
    return result;
  }
  if (pid == 0) {
    if (dup2(input[0], STDIN_FILENO) < 0 || dup2(output[1], STDOUT_FILENO) < 0 || dup2(errors[1], STDERR_FILENO) < 0)
      child_error(exec_errors[1], false);
    input.close_end(0);
    input.close_end(1);
    output.close_end(0);
    output.close_end(1);
    errors.close_end(0);
    errors.close_end(1);
    exec_errors.close_end(0);
    execv(invocation.executable.c_str(), argv.data());
    child_error(exec_errors[1], true);
  }

  input.close_end(0);
  output.close_end(1);
  errors.close_end(1);
  exec_errors.close_end(1);
  if (invocation.stdin_data.empty())
    input.close_end(1);

  SigpipeGuard sigpipe;
  if (int error = sigpipe.block(); error != 0)
    result.error = system_error("block SIGPIPE", error);

  ChildError child_status{};
  size_t child_status_size = 0;
  size_t input_offset = 0;
  std::string input_error;
  std::array<pollfd, 4> fds{
      {{output[0], POLLIN, 0}, {errors[0], POLLIN, 0}, {exec_errors[0], POLLIN, 0}, {input[1], POLLOUT, 0}}};
  while (result.error.empty()) {
    bool any_open = false;
    for (const auto &fd : fds)
      any_open |= fd.fd >= 0;
    if (!any_open)
      break;
    if (poll(fds.data(), fds.size(), -1) < 0) {
      if (errno == EINTR)
        continue;
      result.error = system_error("poll", errno);
      break;
    }
    for (size_t i = 0; i < fds.size(); i++) {
      auto &fd = fds[i];
      if (fd.fd < 0 || fd.revents == 0)
        continue;
      if (fd.revents & POLLNVAL) {
        result.error = "poll: invalid pipe descriptor";
        break;
      }
      if (i == 3) {
        ssize_t count =
            write(fd.fd, invocation.stdin_data.data() + input_offset, invocation.stdin_data.size() - input_offset);
        if (count > 0) {
          input_offset += count;
        } else if (count < 0 && errno != EINTR && errno != EAGAIN) {
          int error = errno;
          if (error == EPIPE) {
            sigpipe.consume();
            input_error = system_error("write(stdin): child closed stdin before complete delivery", error);
          } else {
            result.error = system_error("write(stdin)", error);
          }
          input.close_end(1);
          fd.fd = -1;
        }
        if (input_offset == invocation.stdin_data.size()) {
          input.close_end(1);
          fd.fd = -1;
        }
        continue;
      }
      std::array<char, 4096> buffer;
      ssize_t count = read(fd.fd, buffer.data(), buffer.size());
      if (count > 0) {
        if (i == 0) {
          result.stdout_data.append(buffer.data(), count);
        } else if (i == 1) {
          result.stderr_data.append(buffer.data(), count);
        } else if (child_status_size + count <= sizeof(child_status)) {
          memcpy(reinterpret_cast<char *>(&child_status) + child_status_size, buffer.data(), count);
          child_status_size += count;
        } else {
          result.error = "Invalid child error report";
        }
      } else if (count == 0) {
        if (i == 0) {
          output.close_end(0);
        } else if (i == 1) {
          errors.close_end(0);
        } else {
          exec_errors.close_end(0);
        }
        fd.fd = -1;
      } else if (errno != EINTR && errno != EAGAIN) {
        result.error = system_error("read(pipe)", errno);
        break;
      }
    }
  }

  if (!result.error.empty()) {
    // An internal I/O failure must not leave a child stuck on an unread pipe.
    kill(pid, SIGKILL);
  }
  input.close_end(1);
  output.close_end(0);
  errors.close_end(0);
  exec_errors.close_end(0);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0) {
    if (result.error.empty())
      result.error = system_error("waitpid", errno);
    return result;
  }
  if (!result.error.empty())
    return result;
  if (child_status_size != 0) {
    if (child_status_size != sizeof(child_status)) {
      result.error = "Incomplete child error report";
    } else {
      if (child_status.exec_failed)
        result.termination = CommandTermination::COMMAND_TERMINATION_EXEC_FAILED;
      result.error = system_error(child_status.exec_failed ? "execv" : "dup2", child_status.error_number);
    }
    return result;
  }
  if (WIFEXITED(status)) {
    result.termination = CommandTermination::COMMAND_TERMINATION_EXITED;
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.termination = CommandTermination::COMMAND_TERMINATION_SIGNALED;
    result.signal_number = WTERMSIG(status);
  } else {
    result.error = "Unexpected waitpid status";
  }
  if (!input_error.empty()) {
    result.error = input_error;
    result.termination = CommandTermination::COMMAND_TERMINATION_INTERNAL_ERROR;
  }
  return result;
}

}  // namespace esphome::host_command
