#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

class CDPClient {
public:
  CDPClient(const std::string &host, const std::string &port,
            const std::string &path);
  ~CDPClient();

  // Send a CDP command and wait for the response having
  // the corresponding "id".
  json command(const std::string &method, const json &params = json::object(),
               const std::string &session_id = "");

private:
  asio::io_context io_;
  tcp::resolver resolver_;
  websocket::stream<tcp::socket> ws_;

  int next_id_;
};