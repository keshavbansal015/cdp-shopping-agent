#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using json = nlohmann::json;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;

using tcp = asio::ip::tcp;

// ------------------------------------------------------------
// Start Chromium
// ------------------------------------------------------------

class Chromium {
public:
  Chromium(const std::string &executable, int port) : pid_(-1) {
    pid_ = fork();

    if (pid_ < 0) {
      throw std::runtime_error("fork() failed");
    }

    if (pid_ == 0) {
      // Child process.

      std::string port_arg = "--remote-debugging-port=" + std::to_string(port);

      // A separate profile prevents us from interfering with an
      // already-running Chrome/Chromium instance.
      std::string user_data = "/tmp/cdp-example-profile-" +
                              std::to_string(static_cast<long long>(getpid()));

      execl(executable.c_str(), executable.c_str(),

            "--headless=new", "--disable-gpu", "--no-first-run",
            "--no-default-browser-check", "--disable-background-networking",
            "--disable-extensions", "--disable-sync",

            port_arg.c_str(), ("--user-data-dir=" + user_data).c_str(),

            "about:blank",

            static_cast<char *>(nullptr));

      // Only reached if execl() failed.
      std::perror("execl");
      _exit(127);
    }

    // Give Chromium a moment to initialize.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  ~Chromium() {
    if (pid_ > 0) {
      kill(pid_, SIGTERM);

      // Wait briefly for clean termination.
      for (int i = 0; i < 20; ++i) {
        int status = 0;

        pid_t result = waitpid(pid_, &status, WNOHANG);

        if (result == pid_) {
          pid_ = -1;
          return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      // Force termination if necessary.
      kill(pid_, SIGKILL);
      waitpid(pid_, nullptr, 0);

      pid_ = -1;
    }
  }

  Chromium(const Chromium &) = delete;
  Chromium &operator=(const Chromium &) = delete;

private:
  pid_t pid_;
};

// ------------------------------------------------------------
// HTTP GET
//
// We use HTTP only to obtain Chromium's WebSocket endpoint.
// Actual browser control happens through CDP/WebSocket.
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// Parse ws://127.0.0.1:9222/devtools/browser/...
//
// into host, port, and WebSocket path.
// ------------------------------------------------------------

struct WebSocketEndpoint {
  std::string host;
  std::string port;
  std::string path;
};

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

// ------------------------------------------------------------
// CDP client
// ------------------------------------------------------------

class CDPClient {
public:
  CDPClient(const std::string &host, const std::string &port,
            const std::string &path)
      : resolver_(io_), ws_(io_), next_id_(1) {
    auto endpoints = resolver_.resolve(host, port);

    asio::connect(ws_.next_layer(), endpoints);

    ws_.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::client));

    ws_.handshake(host + ":" + port, path);
  }

  ~CDPClient() {
    beast::error_code ec;

    ws_.close(websocket::close_code::normal, ec);
  }

  // Send a CDP command and wait for the response having
  // the corresponding "id".
  json command(const std::string &method, const json &params = json::object(),
               const std::string &session_id = "") {
    int id = next_id_++;

    json message = {{"id", id}, {"method", method}, {"params", params}};

    if (!session_id.empty()) {
      message["sessionId"] = session_id;
    }

    std::string text = message.dump();

    ws_.write(asio::buffer(text));

    while (true) {
      beast::flat_buffer buffer;

      ws_.read(buffer);

      std::string received = beast::buffers_to_string(buffer.data());

      json response = json::parse(received);

      // CDP also sends asynchronous events. They do not have
      // an "id", so ignore them here.
      if (!response.contains("id")) {
        continue;
      }

      if (response["id"].get<int>() != id) {
        continue;
      }

      if (response.contains("error")) {
        throw std::runtime_error("CDP error: " + response["error"].dump());
      }

      return response;
    }
  }

private:
  asio::io_context io_;
  tcp::resolver resolver_;
  websocket::stream<tcp::socket> ws_;

  int next_id_;
};

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(int argc, char *argv[]) {
  try {
    // Usage:
    //
    //   ./cdp_example /usr/bin/chromium https://example.com
    //
    // Defaults:
    //   chromium
    //   https://example.com

    std::string chromium_path = argc >= 2 ? argv[1] : "chromium";

    std::string url = argc >= 3 ? argv[2] : "https://example.com";

    constexpr int port = 9222;

    std::cout << "Starting Chromium...\n";

    Chromium chromium(chromium_path, port);

    // --------------------------------------------------------
    // Wait for Chromium's debugging HTTP endpoint.
    // --------------------------------------------------------

    std::string version_json;

    for (int attempt = 0; attempt < 50; ++attempt) {
      try {
        version_json =
            http_get("127.0.0.1", std::to_string(port), "/json/version");

        break;
      } catch (...) {
        if (attempt == 49) {
          throw std::runtime_error("Chromium did not start its CDP endpoint");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    json version = json::parse(version_json);

    std::string browser_ws =
        version.at("webSocketDebuggerUrl").get<std::string>();

    std::cout << "CDP endpoint: " << browser_ws << "\n";

    auto endpoint = parse_ws_url(browser_ws);

    // --------------------------------------------------------
    // Connect to the browser-level CDP WebSocket.
    // --------------------------------------------------------

    CDPClient cdp(endpoint.host, endpoint.port, endpoint.path);

    std::cout << "Connected to CDP.\n";

    // --------------------------------------------------------
    // Create a new tab.
    //
    // Target.createTarget is a browser-level CDP command.
    // --------------------------------------------------------

    json target_result =
        cdp.command("Target.createTarget", {{"url", "about:blank"}});

    std::string target_id = target_result["result"]["targetId"];

    std::cout << "Target ID: " << target_id << "\n";

    // --------------------------------------------------------
    // Attach to the target.
    // --------------------------------------------------------

    json attach_result = cdp.command(
        "Target.attachToTarget", {{"targetId", target_id}, {"flatten", true}});

    std::string session_id = attach_result["result"]["sessionId"];

    std::cout << "Session ID: " << session_id << "\n";

    // --------------------------------------------------------
    // Enable domains we are going to use.
    // --------------------------------------------------------

    cdp.command("Page.enable", {}, session_id);

    cdp.command("Runtime.enable", {}, session_id);

    // --------------------------------------------------------
    // Navigate.
    // --------------------------------------------------------

    std::cout << "Navigating to " << url << "...\n";

    cdp.command("Page.navigate", {{"url", url}}, session_id);

    // Simple example: give the page some time to load.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // --------------------------------------------------------
    // Execute JavaScript.
    //
    // This is equivalent to opening DevTools and evaluating:
    //
    //     document.title
    //
    // --------------------------------------------------------

    json evaluation = cdp.command("Runtime.evaluate",
                                  {{"expression", "document.title"},
                                   {"returnByValue", true},
                                   {"awaitPromise", true}},
                                  session_id);

    json remote_object = evaluation["result"]["result"];

    std::cout << "Page title: " << remote_object.dump() << "\n";

    // --------------------------------------------------------
    // Another JavaScript example.
    // --------------------------------------------------------

    json body_text = cdp.command(
        "Runtime.evaluate",
        {{"expression", "document.body ? document.body.innerText : ''"},
         {"returnByValue", true}},
        session_id);

    std::cout << "\nPage text:\n"
              << body_text["result"]["result"]["value"].get<std::string>()
              << "\n";

    // --------------------------------------------------------
    // Close the tab.
    // --------------------------------------------------------

    cdp.command("Target.closeTarget", {{"targetId", target_id}});

    std::cout << "\nDone.\n";
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";

    return 1;
  }

  return 0;
}