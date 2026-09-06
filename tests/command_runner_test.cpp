#include "command_runner.h"

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <poll.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

using namespace esphome::host_command;

enum class Fault { NONE, POLL, READ, FORK, DUP, WAIT_INTERRUPTED };
static Fault fault = Fault::NONE;

// The test is linked with --wrap for these process and I/O functions. Each
// wrapper can inject one deterministic failure while delegating all other calls
// to the real libc function. This exercises cleanup paths that are unreliable
// or impractical to trigger through normal process execution.
extern "C" {
int __real_poll(pollfd *fds, nfds_t count, int timeout);
ssize_t __real_read(int fd, void *buffer, size_t count);
pid_t __real_fork();
int __real_dup2(int old_fd, int new_fd);
pid_t __real_waitpid(pid_t pid, int *status, int options);

int __wrap_poll(pollfd *fds, nfds_t count, int timeout) {
  if (fault == Fault::POLL) {
    errno = EIO;
    return -1;
  }
  return __real_poll(fds, count, timeout);
}
ssize_t __wrap_read(int fd, void *buffer, size_t count) {
  if (fault == Fault::READ) {
    errno = EIO;
    return -1;
  }
  return __real_read(fd, buffer, count);
}
pid_t __wrap_fork() {
  if (fault == Fault::FORK) {
    errno = EAGAIN;
    return -1;
  }
  return __real_fork();
}
int __wrap_dup2(int old_fd, int new_fd) {
  if (fault == Fault::DUP) {
    errno = EBADF;
    return -1;
  }
  return __real_dup2(old_fd, new_fd);
}
pid_t __wrap_waitpid(pid_t pid, int *status, int options) {
  if (fault == Fault::WAIT_INTERRUPTED) {
    fault = Fault::NONE;
    errno = EINTR;
    return -1;
  }
  return __real_waitpid(pid, status, options);
}
}

static void write_all(int fd, const std::string &data) {
  size_t offset = 0;
  while (offset < data.size()) {
    ssize_t count = write(fd, data.data() + offset, data.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    assert(count > 0);
    offset += count;
  }
}

int main(int argc, char **argv) {
  // This executable doubles as the test driver and its child command. The
  // no-argument execution runs the assertions below; run_command() starts a
  // separate copy with one of these modes so child behavior is controlled
  // without relying on shell commands or system-specific utilities.
  if (argc > 1) {
    const std::string mode = argv[1];
    if (mode == "streams") {
      // Fill both output pipes beyond their typical capacity before echoing a
      // large, possibly binary stdin payload. This detects sequential I/O that
      // could deadlock and verifies complete duplex transfer.
      write_all(STDOUT_FILENO, std::string(200000, 'o'));
      write_all(STDERR_FILENO, std::string(200000, 'e'));
      char buffer[4096];
      ssize_t count;
      while ((count = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
        write_all(STDOUT_FILENO, std::string(buffer, count));
      return 0;
    }
    if (mode == "literal") {
      // Return arguments and line endings verbatim to verify that the runner
      // neither invokes a shell nor modifies captured output.
      write_all(STDOUT_FILENO, argv[2]);
      write_all(STDERR_FILENO, "error\r\n");
      return 0;
    }
    // Exit normally with the same value often used by shells for command-not-found.
    if (mode == "exit")
      return 127;
    if (mode == "signal") {
      // Terminate by signal so it can be distinguished from a normal exit code.
      raise(SIGTERM);
      return 1;
    }
    if (mode == "close") {
      // Reject the supplied stdin while keeping stdout usable, provoking EPIPE
      // in the parent without allowing SIGPIPE to terminate the test process.
      close(STDIN_FILENO);
      write_all(STDOUT_FILENO, "closed");
      return 0;
    }
    return 2;
  }

  const std::string executable = argv[0];

  // Test 1: Verify literal argv delivery, separate output streams, and unmodified data.
  auto result = run_command({executable, {"literal", "$HOME | * ; $(false)\r\n"}, ""});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_EXITED);
  assert(result.exit_code == 0);
  assert(result.stdout_data == "$HOME | * ; $(false)\r\n");
  assert(result.stderr_data == "error\r\n");

  // Test 2: Verify concurrent draining of large stdout/stderr streams while a
  // large binary-safe stdin payload is delivered.
  std::string input(300000, 'i');
  input[100] = '\0';
  result = run_command({executable, {"streams"}, input});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_EXITED);
  assert(result.stdout_data == std::string(200000, 'o') + input);
  assert(result.stderr_data == std::string(200000, 'e'));

  // Test 3: Verify that empty input still closes the pipe and delivers EOF.
  result = run_command({executable, {"streams"}, ""});
  assert(result.exit_code == 0);  // Empty input must also deliver EOF.

  // Test 4: Verify that a normal nonzero exit remains a normal exit, including
  // status 127, which shells often use to report command-not-found.
  result = run_command({executable, {"exit"}, ""});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_EXITED);
  assert(result.exit_code == 127 && result.error.empty());

  // Test 5: Verify that execv() reports ENOENT separately from a child exit.
  result = run_command({"/nonexistent-host-command-test", {}, input});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_EXEC_FAILED);
  assert(result.exit_code == -1);
  assert(result.error.find(strerror(ENOENT)) != std::string::npos);

  // Test 6: Verify that execv() preserves a distinct permission error.
  result = run_command({"/", {}, ""});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_EXEC_FAILED);
  assert(result.error.find(strerror(EACCES)) != std::string::npos);

  // Test 7: Verify that signal termination is not converted to an exit code.
  result = run_command({executable, {"signal"}, ""});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_SIGNALED);
  assert(result.signal_number == SIGTERM && result.exit_code == -1);

  // Test 8: A default SIGPIPE disposition must not terminate the calling host
  // when the child closes stdin before the input has been delivered.
  signal(SIGPIPE, SIG_DFL);
  result = run_command({executable, {"close"}, input});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_INTERNAL_ERROR);
  assert(result.error.find(strerror(EPIPE)) != std::string::npos);
  assert(result.stdout_data == "closed");

  // Test 9: Force partial pipe initialization failure, then verify cleanup and recovery.
  rlimit previous;
  assert(getrlimit(RLIMIT_NOFILE, &previous) == 0);
  rlimit limited = previous;
  limited.rlim_cur = 7;
  assert(setrlimit(RLIMIT_NOFILE, &limited) == 0);
  for (int i = 0; i < 10; i++) {
    result = run_command({executable, {"exit"}, ""});
    assert(result.termination == CommandTermination::COMMAND_TERMINATION_INTERNAL_ERROR);
    assert(result.error.find(strerror(EMFILE)) != std::string::npos);
  }
  assert(setrlimit(RLIMIT_NOFILE, &previous) == 0);
  result = run_command({executable, {"exit"}, ""});
  assert(result.exit_code == 127);

  // Test 10a-d: Inject failures at each major syscall stage. Every subcase must
  // report an internal error and reap its child, leaving no zombie for the next
  // subcase. The Fault value identifies the lettered subcase in loop order.
  for (Fault injected : {Fault::POLL, Fault::READ, Fault::FORK, Fault::DUP}) {
    fault = injected;
    result = run_command({executable, {"streams"}, input});
    assert(result.termination == CommandTermination::COMMAND_TERMINATION_INTERNAL_ERROR);
    assert(!result.error.empty());
    fault = Fault::NONE;
    int status;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
  }
  // Test 11: A transient EINTR from waitpid must be retried rather than reported
  // as a command failure.
  fault = Fault::WAIT_INTERRUPTED;
  result = run_command({executable, {"exit"}, ""});
  assert(result.termination == CommandTermination::COMMAND_TERMINATION_EXITED && result.exit_code == 127);

  // Test 12: Run with all standard descriptors initially unavailable. The
  // runner must still construct usable child pipes when they occupy fd 0, 1, or 2.
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  result = run_command({executable, {"literal", "closed host stdio"}, ""});
  assert(result.exit_code == 0 && result.stdout_data == "closed host stdio");
  return 0;
}
