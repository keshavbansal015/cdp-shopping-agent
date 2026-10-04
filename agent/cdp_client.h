#pragma once
#include <string>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

/*
Class: CDPClient

  Usage:
    CDPClient client(host, port, path);
    json result = client.command(method, params, session_id);

  Example:
    CDPClient client("localhost", "9222", "/devtools/browser/");
    json version = client.command("Browser.getVersion");
*/

class CDPClient {
public:
  CDPClient(const std::string &host, const std::string &port,
            const std::string &path);
  ~CDPClient();

  // Send a CDP command and wait for the response having
  // the corresponding "id".
  json command(const std::string &method, const json &params = json::object(),
               const std::string &session_id = "");
  void handle_event(const json &event);

  bool popupShowing() const;
  void clearPopupFlag();

private:
  asio::io_context io_;
  tcp::resolver resolver_;
  websocket::stream<tcp::socket> ws_;

  int next_id_;
  bool popupShowing_ = false;
};