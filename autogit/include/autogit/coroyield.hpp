
#pragma once

#include <boost/asio/post.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/associated_executor.hpp>

using boost::asio::awaitable;
using boost::asio::use_awaitable;

namespace this_coro{

namespace detail {

	struct initiate_coro_yield_op
	{
		template<typename Handler>
		void operator()(Handler&& handler)
		{
			auto executor = boost::asio::get_associated_executor(handler);
			boost::asio::post(executor,
			[handler = std::move(handler)]() mutable
			{
				handler(boost::system::error_code());
			});
		}
	};

}

inline awaitable<void> coro_yield()
{
	co_await boost::asio::post(co_await boost::asio::this_coro::executor, use_awaitable);
}

template<typename Handler>
auto coro_yield(Handler&& handler)
{
	return boost::asio::async_initiate<Handler, void(boost::system::error_code)>(detail::initiate_coro_yield_op{}, handler);
}

}
