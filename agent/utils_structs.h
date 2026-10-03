#pragma once

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

/*
WebSocketEndpoint: Struct to store the WebSocket endpoint.
StepResult: Struct to store the result of a step.
*/

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