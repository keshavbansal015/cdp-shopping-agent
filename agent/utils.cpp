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


std::string http_get(const std::string &host, const std::string &port,
                     const std::string &target) {
  asio::io_context io;
  tcp::resolver resolver(io);
  beast::tcp_stream stream(io);

  auto endpoints = resolver.resolve(host, port);
  stream.connect(endpoints);

  http::request<http::empty_body> request{http::verb::get, target, 11};
  request.set(http::field::host, host + ":" + port);
  request.set(http::field::user_agent, "CDP-Cpp-Example");

  http::write(stream, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);

  if (response.result() != http::status::ok) {
    throw std::runtime_error("HTTP request failed: " +
                             std::to_string(response.result_int()));
  }

  beast::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, ec);

  return response.body();
}