#pragma once

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

struct WebSocketEndpoint {
  std::string host;
  std::string port;
  std::string path;
};

struct StepResult {
  json observation;
  int reward = 0;
  bool done = false;
  bool timedOut = false;
  json info;
};