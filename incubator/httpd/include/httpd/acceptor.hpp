
#pragma once

#include <iostream>
#include <mutex>
#if __has_include("utils/logging.hpp")
#include "utils/logging.hpp"
#define HTTPD_ENABLE_LOGGING 1
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "detail/async_map.hpp"
#include "detail/parser.hpp"
#include "detail/time_clock.hpp"

#include "detail/wait_all.hpp"
#include "detail/config.hpp"

// 每线程一个 accept 的 多 socket accept 模式 适配器.

namespace httpd
{
	using boost::asio::experimental::promise;
	using boost::asio::experimental::use_promise;

	template <typename AcceptedClientClass, typename ServiceClass, typename Execotor = boost::asio::any_io_executor>
	requires detail::is_tcpsocket_server_class<ServiceClass, AcceptedClientClass>
	class acceptor
	{
		ServiceClass& executor_;
		boost::asio::ip::tcp::acceptor accept_socket_;
		std::unordered_map<std::size_t, AcceptedClientClass> all_client;
		bool accepting = false;

	public:
		acceptor(const acceptor&) = delete;
		explicit acceptor(acceptor&& o)
			: executor_(o.executor_)
			, accept_socket_(std::move(o.accept_socket_))
			, accepting(o.accepting)
		{
			BOOST_ASSERT_MSG(!o.accepting, "cannot move a socket that has pending IO");
		};

		template <typename AnyExecutor>
		explicit acceptor(AnyExecutor&& e, ServiceClass& executor)
			: executor_(executor)
			, accept_socket_(e)
		{
		}

		template <typename AnyExecutor>
		explicit acceptor(AnyExecutor&& e, ServiceClass& executor, std::string listen_address)
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
			using tcp = boost::asio::ip::tcp;
			tcp::endpoint endp;

			bool ipv6only = httpd::detail::make_listen_endpoint(listen_address, endp, ec);
			if (ec)
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_ERR << "WS server listen error: " << listen_address << ", ec: " << ec.message();
#endif
				return;
			}

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

#ifdef SO_REUSEPORT
			if constexpr (has_so_reuseport())
			{
				accept_socket_.set_option(socket_options::reuse_port(true), ec);
				if (ec)
				{
#ifdef HTTPD_ENABLE_LOGGING
					LOG_ERR << "WS server accept set option SO_REUSEPORT failed: " << ec.message();
#endif
					return;
				}
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

			if (ipv6only)
			{
				accept_socket_.set_option(boost::asio::ip::v6_only(true), ec);
				if (ec)
				{
#ifdef HTTPD_ENABLE_LOGGING
					LOG_ERR << "WS server setsockopt IPV6_V6ONLY";
#endif
					return;
				}
			}

			accept_socket_.bind(endp, ec);
			if (ec)
			{
#ifdef HTTPD_ENABLE_LOGGING
				LOG_ERR << "WS server bind failed: " << ec.message() << ", address: " << endp.address().to_string()
						<< ", port: " << endp.port();
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
		}

		boost::asio::awaitable<void> run_accept_loop(int number_of_concurrent_acceptor)
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
				[number_of_concurrent_acceptor, this]() mutable -> boost::asio::awaitable<void>
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
				},
				boost::asio::use_awaitable);
#ifdef HTTPD_ENABLE_LOGGING
			LOG_DBG << "accepting loop exited";
#endif
		}

		boost::asio::awaitable<void> clean_shutdown()
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
				co_await t.async_wait(boost::asio::use_awaitable);
			}
#ifdef HTTPD_ENABLE_LOGGING
			LOG_DBG << "clean_shutdown exit";
#endif
			co_return;
		}

	private:
		boost::asio::awaitable<void> accept_loop()
		{
			static std::atomic_size_t id{ 0 };

			while (true)
			{
				size_t connection_id = id++;

				boost::system::error_code error;

				auto client_ptr = executor_.make_shared_connection(get_executor(), connection_id);

				co_await accept_socket_.async_accept(
					client_ptr->socket(), boost::asio::redirect_error(boost::asio::use_awaitable, error));

				if (error == boost::asio::error::operation_aborted || error == boost::asio::error::bad_descriptor)
				{
					co_return;
				}
				if (error)
				{
					continue;
				}

#ifdef BOOST_POSIX_API
				int fd = client_ptr->socket().native_handle();
				int flags = fcntl(fd, F_GETFD);
				flags |= FD_CLOEXEC;
				fcntl(fd, F_SETFD, flags);
#endif
				client_ptr->socket().set_option(boost::asio::socket_base::keep_alive(true), error);

				std::string remote_host;
				auto endp = client_ptr->socket().remote_endpoint(error);
				if (!error)
				{
					if (endp.address().is_v6())
					{
						remote_host = "[" + endp.address().to_string() + "]:" + std::to_string(endp.port());
					}
					else
					{
						remote_host = endp.address().to_string() + ":" + std::to_string(endp.port());
					}
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