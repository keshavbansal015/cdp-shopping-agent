#include "cdp_client.h"

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

/*
CDPClient: A client for the Chrome DevTools Protocol (CDP).

Usage:
  CDPClient client(host, port, path);
  json result = client.command(method, params, session_id);

Example:
  CDPClient client("localhost", "9222", "/devtools/browser/");
  json version = client.command("Browser.getVersion");
*/

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
CDPClient::json CDPClient::command(const std::string &method, const json &params,
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