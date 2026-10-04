#include <iostream>
#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "chromium_process.h"
#include "cdp_client.h"
#include "utils.h"
#include "params.h"
#include "agent.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;

using tcp = asio::ip::tcp;


// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(int argc, char *argv[]) {
  try {
    const std::string chromium = argc >= 2 ? argv[1] : "chromium";

    const std::string logFile = argc >= 3 ? argv[2] : "agent.jsonl";

    Agent agent(chromium, logFile);

    // ----------------------------------------------------
    // Example episode
    // ----------------------------------------------------

    std::string url = generateTest()[0];

    json observation = agent.reset(url, 42);

    std::cout << "reset:\n" << observation.dump(2) << "\n";

    // An actual RL agent would choose these actions.
    //
    // They are only examples demonstrating the API.
    std::vector<std::string> actions = {"wait", "click(0)"};

    for (const auto &action : actions) {
      StepResult result = agent.step(action);

      std::cout << "step: " << action << "\n"
                << result.observation.dump(2) << "\n"
                << "reward=" << result.reward << " done=" << std::boolalpha
                << result.done << " timed_out=" << result.timedOut << "\n";

      if (result.done)
        break;
    }

    // Agent destructor terminates Chromium.
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "fatal: " << e.what() << '\n';

    return 1;
  }
}