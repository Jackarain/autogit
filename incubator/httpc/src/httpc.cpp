#include "httpc/httpc.hpp"

namespace httpc {

	static const std::string chrome_user_agent = R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/74.0.3729.169 Safari/537.36)";
	static const std::string edge_user_agent = R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/64.0.3282.140 Safari/537.36 Edge/18.17763)";
	static const std::string ie_user_agent = R"(Mozilla/5.0 (Windows NT 10.0; WOW64; Trident/7.0; rv:11.0) like Gecko)";
	static const std::string curl_user_agent = R"(curl/7.64.0)";

    http_client::http_client(net::any_io_executor ex)
        : executor_(ex)
    {
    }

    http_client::~http_client()
    {}

    net::awaitable<http_result> http_client::async_perform(
        const std::string& url, const http_request& req)
    {
        boost::system::error_code ec;

        // variant_socket newsocket(boost::make_unique<tcp_stream>(executor_));
        // stream_socket_.emplace<tcp_stream_ptr>(boost::make_unique<tcp_stream>(executor_));

        // net::ssl::context ssl_ctx(net::ssl::context::tls_client);
        // stream_socket_.emplace<ssl_stream_ptr>(boost::make_unique<ssl_stream>(executor_, ssl_ctx));

        co_return ec;
    }

    void http_client::set_download_file(const std::string &file_path) noexcept
    {
        download_file_path_ = file_path;
    }

    void http_client::set_transfer_handler(transfer_handler &&handler) noexcept
    {
        transfer_handler_ = std::move(handler);
    }

    bool http_client::check_certificate() const noexcept
    {
		return check_certificate_;
	}

    void http_client::check_certificate(bool check) noexcept
    {
        check_certificate_ = check;
    }

    net::any_io_executor http_client::get_executor() noexcept
    {
        return executor_;
    }

}
