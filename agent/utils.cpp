#include "utils.h"
#include <random>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>


namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

std::vector<std::string> generateTest() {
  using namespace std;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> distrib(0.0, 1.0);
  std::uniform_int_distribution<int> product_dist(0, 3);
  std::uniform_int_distribution<int> quantity_dist(0, 2);
  const std::vector<std::string> products(
      {"blue-mug", "red-hat", "green-lamp", "yellow-notebook"});
  const int quantities[] = {1, 2, 3};

  const int seed = 42;
  double popup_p = distrib(gen);
  double delay_p = distrib(gen);

  std::string root_url = "file:///Users/keshavbansal/keshav/dev_test/"
                         "cdp-shopping-agent/site/index.html?seed=" +
                         std::to_string(seed);

  std::vector<std::string> test;

  for (int i = 1; i <= 1; i++) {
    double popup_p = distrib(gen);
    double delay_p = distrib(gen);
    std::string product = products[product_dist(gen)];
    int quantity = quantities[quantity_dist(gen)];
    std::string url =
        root_url + "&item=" + product + "&qty=" + std::to_string(quantity) +
        "&popup_p=" + std::to_string(popup_p) + "&delay_p=" + std::to_string(delay_p);
    test.push_back(url);
  }
  return test;
}

/*
readScript: Read the content of a file and return it as a string.
filename: The name of the file to read.
Returns: The content of the file as a string.
example: 
string script = readScript("observe_js_script.txt");
// the file content will be like
(function() { ... })();
*/
std::string readScript(const std::string &filename) {
  std::string script_content;
  std::ifstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filename);
  }
  std::string line;
  while (std::getline(file, line)) {
    script_content += line + "\n";
  }
  file.close();
  return script_content;
}

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
