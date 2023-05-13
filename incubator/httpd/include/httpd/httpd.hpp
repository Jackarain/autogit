
#pragma once

#include <map>
#include <string>

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

#include "httpd/http_stream.hpp"

using boost::asio::awaitable;


namespace httpd {
    awaitable<boost::system::error_code> send_string_response_body(http_any_stream& client,
        std::string res_body,
        std::map<boost::beast::http::field, std::string> headers, int http_version, bool keepalive, boost::beast::http::status status = boost::beast::http::status::ok);

}
