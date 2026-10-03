#include "WebSocketEndpoint.h"

/*
parse_ws_url: Parse the WebSocket URL and return the host, port, and path.
url: The WebSocket URL to parse.
Example: ws://[IP_ADDRESS]/devtools/browser/
Returns: WebSocketEndpoint{host, port, path}
*/
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
    // default port 80
    host = host_port;
    port = "80";
  } else {
    // extract host and port
    host = host_port.substr(0, colon);
    port = host_port.substr(colon + 1);
  }
  return {host, port, rest.substr(slash)};
}