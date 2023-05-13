
#pragma once

#include <concepts>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/promise.hpp>

#include "./concepts.hpp"

namespace httpd::detail {

    template <typename Range> requires ( is_range<Range> && is_waitable<typename std::decay_t<Range>::value_type> )
    boost::asio::awaitable<void> wait_all(Range&& range)
    {
		for (auto& item : range)
			co_await item.async_wait(boost::asio::use_awaitable);
    }

    template <typename Range> requires ( is_range<Range> && is_awaitable<typename std::decay_t<Range>::value_type> )
    boost::asio::awaitable<void> wait_all(Range&& range)
    {
		for (auto& item : range)
			co_await item(boost::asio::use_awaitable);
    }
}
