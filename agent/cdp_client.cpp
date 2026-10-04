#include "cdp_client.h"

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;

CDPClient::CDPClient(const std::string &host, const std::string &port,
                     const std::string &path)
    : resolver_(io_), ws_(io_), next_id_(1) {
  auto endpoints = resolver_.resolve(host, port);

  asio::connect(ws_.next_layer(), endpoints);

  ws_.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::client));

  ws_.handshake(host + ":" + port, path);
}

CDPClient::~CDPClient() {
  beast::error_code ec;

  ws_.close(websocket::close_code::normal, ec);
}

// Send a CDP command and wait for the response having
// the corresponding "id".
json CDPClient::command(const std::string &method, const json &params,
                        const std::string &session_id) {

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
      handle_event(response);
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
void CDPClient::handle_event(const json &event) {
  if (!event.contains("method"))
    return;

  const std::string method = event["method"].get<std::string>();

  if (method == "Page.javascriptDialogOpening") {
    popupShowing_ = true;

    try {
      command("Page.handleJavaScriptDialog", {{"accept", false}});
    } catch (const std::runtime_error &e) {
      // Ignore errors from handleJavaScriptDialog
    }
  }
}

bool CDPClient::popupShowing() const { return popupShowing_; }

void CDPClient::clearPopupFlag() { popupShowing_ = false; }