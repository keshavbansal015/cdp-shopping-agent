#pragma once

#include "utils_structs.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <stdexcept>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

WebSocketEndpoint parse_ws_url(const std::string &url);
std::string http_get(const std::string &host, const std::string &port,
                     const std::string &target);
static bool startsWith(const std::string &s, const std::string &prefix);
static bool endsWith(const std::string &s, const std::string &suffix);
static long long elapsedMs(const std::chrono::steady_clock::time_point &start);
