
#pragma once

#include <iostream>
#include <mutex>
#if __has_include("utils/logging.hpp")
#include "utils/logging.hpp"
#define HTTPD_ENABLE_LOGGING 1
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "detail/async_map.hpp"
#include "detail/parser.hpp"
#include "detail/time_clock.hpp"

#include "detail/wait_all.hpp"

// 每线程一个 accept 的 多 socket accept 模式 适配器.

#include "detail/config.hpp"

using boost::asio::awaitable;
using boost::asio::use_awaitable;

namespace httpd
{
	using boost::asio::experimental::promise;
	using boost::asio::experimental::use_promise;

	template <typename AcceptedClientClass, typename ServiceClass, typename Execotor = boost::asio::any_io_executor>
	requires detail::is_unixsocket_server_class<ServiceClass, AcceptedClientClass>
	class unix_acceptor
	{
		ServiceClass& executor_;
		boost::asio::local::stream_protocol::acceptor accept_socket_;
		std::unordered_map<std::size_t, AcceptedClientClass> all_client;
		bool accepting = false;

	public:
		unix_acceptor(const unix_acceptor&) = delete;
		explicit unix_acceptor(unix_acceptor&& o)
			: executor_(o.executor_)
			, accept_socket_(std::move(o.accept_socket_))
			, accepting(o.accepting)
		{
			BOOST_ASSERT_MSG(!o.accepting, "cannot move a socket that has pending IO");
		};

		template <typename AnyExecutor>
		explicit unix_acceptor(AnyExecutor&& e, ServiceClass& executor)
			: executor_(executor)
			, accept_socket_(e)
		{
		}

		template <typename AnyExecutor>
		explicit unix_acceptor(AnyExecutor&& e, ServiceClass& executor, std::string listen_address)
			: executor_(executor)
			, accept_socket_(e)
		{
			this->listen(listen_address);
		}

		auto get_executor() { return accept_socket_.get_executor(); }

		void listen(std::string listen_address)
		{
			boost::system::error_code ec;
			this->listen(listen_address, ec);
			if (ec)
				throw boost::system::system_error(ec);
		}

		void listen(std::string listen_address, boost::system::error_code& ec)
		{
			boost::asio::local::stream_protocol::endpoint endp;

			endp.path(listen_address);

			std::filesystem::remove(listen_address);

			accept_socket_.open(endp.protocol(), ec);
			if (ec)
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_ERR << "WS server open accept error: " << ec.message();
#endif
				return;
			}

#ifdef BOOST_POSIX_API
			int fd = accept_socket_.native_handle();
			int flags = fcntl(fd, F_GETFD);
			if (! (flags & FD_CLOEXEC))
			{
				flags |= FD_CLOEXEC;
				fcntl(fd, F_SETFD, flags);
			}
#endif

			accept_socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
			if (ec)
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_ERR << "WS server accept set option failed: " << ec.message();
#endif
				return;
			}

			accept_socket_.bind(endp, ec);
			if (ec)
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_ERR << "WS server bind failed: " << ec.message() << ", address: " << endp.path();
#endif
				return;
			}

			accept_socket_.listen(boost::asio::socket_base::max_listen_connections, ec);
			if (ec)
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_ERR << "WS server listen failed: " << ec.message();
#endif
				return;
			}
			std::filesystem::permissions(listen_address, std::filesystem::perms::all);
		}

		awaitable<void> run_accept_loop(int number_of_concurrent_acceptor)
		{
			boost::asio::cancellation_state cs = co_await boost::asio::this_coro::cancellation_state;

			cs.slot().assign(
				[this](boost::asio::cancellation_type_t) mutable
				{
					boost::system::error_code ignore_ec;
					accept_socket_.cancel(ignore_ec);
				});

			co_await boost::asio::co_spawn(
				get_executor(),
				[number_of_concurrent_acceptor, this]() mutable -> awaitable<void>
				{
					std::vector<promise<void(std::exception_ptr)>> co_threads;
					// 避免 object 发生移动.
					co_threads.reserve(number_of_concurrent_acceptor);

					for (int i = 0; i < number_of_concurrent_acceptor; i++)
					{
						co_threads.emplace_back(boost::asio::co_spawn(
							this->get_executor(), accept_loop(), use_promise));
					}

					co_await detail::wait_all(co_threads);
				}, use_awaitable);
#ifdef HTTPD_ENABLE_LOGGING
			LOG_DBG << "accepting loop exited";
#endif
		}

		awaitable<void> clean_shutdown()
		{
			for (auto& ws : all_client)
			{
				auto& conn_ptr = ws.second;
				conn_ptr->close();
			}

			while (all_client.size())
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_DBG << "waiting for client to exit";
#endif
				using timer = boost::asio::basic_waitable_timer<time_clock::steady_clock>;
				timer t(get_executor());
				t.expires_from_now(std::chrono::milliseconds(20));
				co_await t.async_wait(use_awaitable);
			}
#ifdef HTTPD_ENABLE_LOGGING
			LOG_DBG << "clean_shutdown exit";
#endif
			co_return;
		}

	private:
		awaitable<void> accept_loop()
		{
			static std::atomic_size_t id{ 0 };

			while (true)
			{
				size_t connection_id = id++;

				boost::system::error_code error;

				auto client_ptr = executor_.make_shared_unixsocket_connection(get_executor(), connection_id);

				co_await accept_socket_.async_accept(
					client_ptr->unix_socket(), boost::asio::redirect_error(use_awaitable, error));

				if (error == boost::asio::error::operation_aborted || error == boost::asio::error::bad_descriptor)
				{
					co_return;
				}
				if (error)
				{
					continue;
				}

#ifdef BOOST_POSIX_API
				int fd = client_ptr->unix_socket().native_handle();
				int flags = fcntl(fd, F_GETFD);
				flags |= FD_CLOEXEC;
				fcntl(fd, F_SETFD, flags);
#endif
				std::string remote_host;
				auto endp = client_ptr->unix_socket().remote_endpoint(error);
				if (!error)
				{
					remote_host = "{" + endp.path() + "}";
				}

				client_ptr->remote_host_ = remote_host;

				all_client.insert({ connection_id, client_ptr });

				boost::asio::co_spawn(client_ptr->get_executor(),
					executor_.client_connected(client_ptr),
					[this, connection_id, client_ptr](std::exception_ptr)
					{
						all_client.erase(connection_id);
						boost::asio::co_spawn(
							get_executor(), executor_.client_disconnected(client_ptr), boost::asio::detached);
					});
			}
		}
	};

}