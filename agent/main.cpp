#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "chromium_process.h"
#include "cdp_client.h"
#include "utils.h"
#include "params.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;

using tcp = asio::ip::tcp;

// ------------------------------------------------------------
// HTTP GET
//
// We use HTTP only to obtain Chromium's WebSocket endpoint.
// Actual browser control happens through CDP/WebSocket.
// ------------------------------------------------------------



// ------------------------------------------------------------
// Parse ws://127.0.0.1:9222/devtools/browser/...
//
// into host, port, and WebSocket path.
// ------------------------------------------------------------

// ------------------------------------------------------------
// CDP client
// ------------------------------------------------------------



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