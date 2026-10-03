#pragma once

#include <string>

struct WebSocketEndpoint {
  std::string host;
  std::string port;
  std::string path;
};