#pragma once

#include "utils_structs.h"

#include <chrono>
#include <stdexcept>
#include <string>


std::vector<std::string> generateTest();
WebSocketEndpoint parse_ws_url(const std::string &url);
std::string http_get(const std::string &host, const std::string &port,
                     const std::string &target);
inline bool startsWith(const std::string &s, const std::string &prefix) {
  return s.rfind(prefix, 0) == 0;
}

inline bool endsWith(const std::string &s, const std::string &suffix) {
  if (suffix.size() > s.size())
    return false;
  return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline long long elapsedMs(const std::chrono::steady_clock::time_point &start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}
