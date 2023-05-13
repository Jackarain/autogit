
#pragma once

#include <type_traits>
#include <concepts>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

namespace httpd::detail {

    template<typename T, typename... Args>
    concept is_coroutine_function_with_args = std::convertible_to<typename std::invoke_result<T, Args...>::type, boost::asio::awaitable<void>>;

    template<typename T>
    concept is_coroutine_function = requires (T t)
    {
        { t() } -> std::convertible_to<boost::asio::awaitable<void>>; // 函数返回 boost::asio::awaitable<void>
    };

    template<typename T>
    concept is_range = requires (T t)
    {
        { ++t.begin() }; // begin 是个前向迭代器
        { t.end() }; // 有 end
        { t.end() - t.begin() } -> std::convertible_to<std::size_t>; // 前后迭代器可以推算 range 大小
    };

    template <typename T>
    concept is_asio_io_object = requires (T t)
    {
        { t.get_executor() }; // 有 get_executor() 成员函数的就是 asio 的 io 对象.
    };

    template <typename T>
    concept is_waitable = requires (T t)
    {
        { t.async_wait(boost::asio::use_awaitable) }; // 有 async_wait() 成员函数的就是 waitable 对象.
    };

    template <typename T>
    concept is_awaitable = requires (T t)
    {
        { t(boost::asio::use_awaitable) }; // 有 async_wait() 成员函数的就是 waitable 对象.
    };

    template <typename T>
    concept is_httpd_server = requires (T t)
    {
        { t.get_executor() };
        { t.make_shared_connection(t.get_executor(), 0) }; // 有 make_shared_connection 成员
        { t.client_connected(t.make_shared_connection(t.get_executor(), 0)) }; // 有 client_connected 成员
        { t.client_disconnected(t.make_shared_connection(t.get_executor(), 0)) }; // 有 client_disconnected 成员
    };

    template <typename T>
    concept is_httpsd_server = requires (T t)
    {
        { t.get_executor() };
        { t.make_shared_ssl_connection(t.get_executor(), 0) }; // 有 make_shared_ssl_connection 成员
        { t.client_connected(t.make_shared_ssl_connection(t.get_executor(), 0)) }; // 有 client_connected 成员
        { t.client_disconnected(t.make_shared_ssl_connection(t.get_executor(), 0)) }; // 有 client_disconnected 成员
    };

    template <typename T>
    concept is_unix_socket_httpd_server = requires (T t)
    {
        { t.get_executor() };
        { t.make_shared_unixsocket_connection(t.get_executor(), 0) }; // 有 make_shared_ssl_connection 成员
        { t.client_connected(t.make_shared_unixsocket_connection(t.get_executor(), 0)) }; // 有 client_connected 成员
        { t.client_disconnected(t.make_shared_unixsocket_connection(t.get_executor(), 0)) }; // 有 client_disconnected 成员
    };

    template <typename T>
    concept is_httpd_client = requires (T t)
    {
        { t.get_executor() };
        { t.socket() } -> std::same_as<boost::asio::ip::tcp::socket&>;
        { t.remote_host_ } -> std::convertible_to<std::string>;
    };

    template <typename T>
    concept is_unix_socket_httpd_client = requires (T t)
    {
        { t.get_executor() };
        { t.unix_socket() } -> std::same_as<boost::asio::local::stream_protocol::socket&>;
        { t.remote_host_ } -> std::convertible_to<std::string>;
    };

    template <typename ServiceClass, typename AcceptedClientClass>
    concept is_tcpsocket_server_class = requires (ServiceClass t)
    {
		{ detail::is_httpd_server<ServiceClass> };
		// 访问 element_type 表明, AcceptedClientClass 必须得是一个智能指针类型.
		{ detail::is_httpd_client<typename AcceptedClientClass::element_type> };
    };

    template <typename ServiceClass, typename AcceptedClientClass>
    concept is_tcpssl_server_class = requires (ServiceClass t)
    {
		{ detail::is_httpsd_server<ServiceClass> };
		// 访问 element_type 表明, AcceptedClientClass 必须得是一个智能指针类型.
		{ detail::is_httpd_client<typename AcceptedClientClass::element_type> };
    };

    template <typename ServiceClass, typename AcceptedClientClass>
    concept is_unixsocket_server_class = requires (ServiceClass t)
    {
		{ detail::is_httpsd_server<ServiceClass> };
		// 访问 element_type 表明, AcceptedClientClass 必须得是一个智能指针类型.
		{ detail::is_unix_socket_httpd_client<typename AcceptedClientClass::element_type> };
    };
}
