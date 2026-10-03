#pragma once
#include "WebSocketEndpoint.h"

WebSocketEndpoint parse_ws_url(const std::string &url);
std::string http_get(const std::string &host, const std::string &port,
                     const std::string &target);