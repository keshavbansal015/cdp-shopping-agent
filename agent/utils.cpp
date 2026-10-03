#include "WebSocketEndpoint.h"

WebSocketEndpoint parse_ws_url(const std::string &url) {
  const std::string prefix = "ws://";

  if (url.rfind(prefix, 0) != 0) {
    throw std::runtime_error("Only ws:// CDP endpoints are supported");
  }

  std::string rest = url.substr(prefix.size());

  auto slash = rest.find('/');

  if (slash == std::string::npos) {
    throw std::runtime_error("Invalid WebSocket URL: " + url);
  }

  std::string host_port = rest.substr(0, slash);

  std::string host;
  std::string port;

  auto colon = host_port.rfind(':');

  if (colon == std::string::npos) {
    host = host_port;
    port = "80";
  } else {
    host = host_port.substr(0, colon);
    port = host_port.substr(colon + 1);
  }

  return {host, port, rest.substr(slash)};
}