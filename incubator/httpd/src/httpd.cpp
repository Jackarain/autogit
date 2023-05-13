
#include <map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/awaitable.hpp>

#include "httpd/httpd.hpp"

using boost::asio::awaitable;

awaitable<boost::system::error_code> httpd::send_string_response_body(http_any_stream& client,
        std::string res_body, std::map<boost::beast::http::field, std::string> headers, int http_version, bool keepalive, boost::beast::http::status status)
{
    boost::beast::http::response<boost::beast::http::string_body> res{ status, http_version };

    res.set(boost::beast::http::field::server, "cmall1.0");
    for (auto h : headers)
        res.set(h.first, h.second);
    res.keep_alive(keepalive);

    boost::system::error_code ec;
    res.body() = res_body;

    res.prepare_payload();

    boost::beast::http::response_serializer<boost::beast::http::string_body, boost::beast::http::fields> sr{ res };

    co_await boost::beast::http::async_write(client, sr, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    co_return ec;
}
