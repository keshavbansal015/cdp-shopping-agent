#pragma once

#include <stdexcept>
#include <string>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <csignal>
#include <sys/wait.h>

class Chromium{
public:
  Chromium() = default;

  void start(const std::string &executable, int port);

  void stop();

  ~Chromium();

  Chromium(const Chromium &) = delete;
  Chromium &operator=(const Chromium &) = delete;

private:
  pid_t pid_ = -1;
};