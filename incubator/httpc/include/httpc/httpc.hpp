//
// httpc.hpp
// ~~~~~~~~~
//
// Copyright (c) 2026 Jack (jack dot wgm at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <boost/variant2.hpp>
#include <boost/system/result.hpp>


namespace httpc {

    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;

    using tcp = net::ip::tcp;

    using http_request = http::request<http::string_body>;
    using http_response = http::response<http::dynamic_body>;

    template<class T>
    using result = boost::system::result<T>;

    using http_result = result<http_response>;

    class http_client
    {
        using transfer_handler = std::function<void(const void*, std::size_t)>;

    public:
        using ssl_stream = beast::ssl_stream<beast::tcp_stream>;
        using ssl_stream_ptr = std::unique_ptr<ssl_stream>;

		using tcp_stream = beast::tcp_stream;
        using tcp_stream_ptr = std::unique_ptr<tcp_stream>;

		using variant_socket = boost::variant2::variant<tcp_stream_ptr, ssl_stream_ptr>;

        using executor_type = net::any_io_executor;

    public:
        // 构造
        explicit http_client(net::any_io_executor ex);
        ~http_client();

    public:
        // 异步执行 HTTP 请求.
        net::awaitable<http_result> async_perform(
            const std::string& url, const http_request& req);

        // 设置下载到指定文件.
        void set_download_file(const std::string& file_path) noexcept;

        // 设置传输回调函数.
        void set_transfer_handler(transfer_handler&& handler) noexcept;

    	// 检查和设置证书认证是否启用.
		bool check_certificate() const noexcept;
		void check_certificate(bool check) noexcept;

        //
		net::any_io_executor get_executor() noexcept;

    private:
        //
        net::any_io_executor executor_;

        //
        variant_socket stream_socket_;

        //
        std::string download_file_path_;

        //
        transfer_handler transfer_handler_;

        //
        bool check_certificate_{ false };
    };
}
