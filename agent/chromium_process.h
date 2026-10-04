#pragma once

#include <stdexcept>
#include <string>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <csignal>
#include <sys/wait.h>

/*
Class to handle the lifecycle of the Chromium process.
*/

class Chromium{
public:
  Chromium() = default;
  ~Chromium();

  void start(const std::string &executable, int port);
  void stop();

  Chromium(const Chromium &) = delete;
  Chromium &operator=(const Chromium &) = delete;

private:
  pid_t pid_ = -1;
};