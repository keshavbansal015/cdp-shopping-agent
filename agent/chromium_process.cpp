#include "chromium_process.h"

#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

void Chromium::start(const std::string &executable, int port) {
  if (pid_ > 0)
    throw std::runtime_error("Chromium already running");

  pid_ = fork();

  if (pid_ < 0) {
    throw std::runtime_error("fork() failed");
  }

  if (pid_ == 0) {
    // Child process.

    std::string port_arg = "--remote-debugging-port=" + std::to_string(port);

    // A separate profile prevents us from interfering with an
    // already-running Chrome/Chromium instance.
    std::string user_data = "/tmp/cdp-example-profile-" +
                            std::to_string(static_cast<long long>(getpid()));

    execl(executable.c_str(), executable.c_str(), "--headless=new",
          "--disable-gpu", "--no-first-run", "--no-default-browser-check",
          "--disable-background-networking", "--disable-extensions",
          "--disable-sync", port_arg.c_str(),
          ("--user-data-dir=" + user_data).c_str(), "about:blank",
          static_cast<char *>(nullptr));

    // Only reached if execl() failed.
    std::perror("execl");
    _exit(127);
  }

  // Give Chromium a moment to initialize.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void Chromium::stop() {
  if (pid_ > 0) {
    kill(pid_, SIGTERM);

    // Wait briefly for clean termination.
    for (int i = 0; i < 20; ++i) {
      int status = 0;

      pid_t result = waitpid(pid_, &status, WNOHANG);

      if (result == pid_) {
        pid_ = -1;
        return;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Force termination if necessary.
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);

    pid_ = -1;
  }
}

Chromium::~Chromium() {
  stop();
}
