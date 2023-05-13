
#pragma once

#include <boost/variant2.hpp>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace httpd {

using namespace boost::variant2;

typedef boost::beast::basic_stream<boost::asio::local::stream_protocol> unix_stream;

// 把 ssl 和 非 ssl 封成一个 variant.
template <typename... StreamTypes>
class http_stream : public variant<StreamTypes...>
{
public:
    using variant<StreamTypes...>::variant;
    typedef boost::asio::any_io_executor executor_type;

    auto get_executor()
    {
        return visit([](auto && realtype) mutable {
            return realtype.get_executor();
        }, *this);
    }

    template<typename MutableBufferSequence, typename H>
    auto async_read_some(const MutableBufferSequence& b, H&& handler)
    {
        return visit([&b, handler = std::forward<H>(handler)](auto && realtype) mutable -> void {
            return realtype.async_read_some(b, std::forward<H>(handler));
        }, *this);
    }

    template<typename ConstBufferSequence, typename H>
    auto async_write_some(const ConstBufferSequence& b, H&& handler)
    {
        visit([&b, handler = std::forward<H>(handler)](auto && realtype) mutable -> void {
            return realtype.async_write_some(b, std::forward<H>(handler));
        }, *this);
    }

    auto close()
    {
        return visit([](auto && realtype) mutable {
            return boost::beast::get_lowest_layer(realtype).close();
        }, *this);
    }

    auto expires_after(auto expiry_time)
    {
        return visit([expiry_time](auto && realtype) mutable {
            return boost::beast::get_lowest_layer(realtype).expires_after(expiry_time);
        }, *this);
    }

    boost::asio::ip::tcp::socket& socket()
    {
        return visit([](auto && realtype) mutable -> boost::asio::ip::tcp::socket& {
            if constexpr (std::is_same_v<std::decay_t<decltype(realtype)>, httpd::unix_stream>)
                throw std::runtime_error("not a tcp socket");
            else
                return boost::beast::get_lowest_layer(realtype).socket();
        }, *this);
    }

    boost::asio::local::stream_protocol::socket& unix_socket()
    {
        return visit([](auto && realtype) mutable -> boost::asio::local::stream_protocol::socket& {
            if constexpr (std::is_same_v<std::decay_t<decltype(realtype)>, httpd::unix_stream>)
                return boost::beast::get_lowest_layer(realtype).socket();
            else
                throw std::runtime_error("not a unix_socket");
        }, *this);
    }

    template<class TeardownHandler>
    auto async_teardown(boost::beast::role_type role, TeardownHandler&& handler)
    {
        return boost::asio::async_initiate<TeardownHandler, void(const boost::system::error_code&)>(
            [role, this](auto&& handler) mutable {
                return visit([role, handler = std::move(handler)](auto&& realtype) mutable {
                    boost::beast::async_teardown(role, realtype, std::move(handler));
                }, *this);
            }, handler);
    }
};

typedef http_stream<boost::beast::tcp_stream, boost::beast::ssl_stream<boost::beast::tcp_stream>, unix_stream> http_any_stream;

}

namespace boost::beast{

    template <typename... StreamTypes>
    void beast_close_socket(httpd::http_stream<StreamTypes...>& s)
    {
        s.close();
    }

    template<class TeardownHandler, typename... UnderlayingScokets>
    void async_teardown(
        role_type role,
        httpd::http_stream<UnderlayingScokets...>& socket,
        TeardownHandler&& handler)
    {
        socket.async_teardown(role, std::forward<TeardownHandler>(handler));
    }

}
