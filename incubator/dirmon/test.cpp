
#include "dirmon/dirmon.hpp"
#include <iostream>
#include <boost/asio/experimental/awaitable_operators.hpp>

using awaitable_timer = boost::asio::use_awaitable_t<>::as_default_on_t<boost::asio::steady_timer>;

boost::asio::awaitable<int> co_main(int argc, char** argv, boost::asio::io_context& io)
{
	dirmon::dirmon git_monitor(io, ".");

	while(true)
	{
		// auto changed_list = co_await monitor.async_wait_dirchange();
		using namespace boost::asio::experimental::awaitable_operators;
		using namespace std::chrono_literals;

		awaitable_timer timer(co_await boost::asio::this_coro::executor);
		timer.expires_from_now(3s);

		auto awaited_result = co_await (
			timer.async_wait()
				||
			git_monitor.async_wait_dirchange()
		);

		if (awaited_result.index() == 1)
		{
			auto changed_list = std::get<1>(awaited_result);
			for (auto& c : changed_list)
			{
				std::cout << "got change. filename: " << c.file_name << "\n";
			}
		}
		else
		{
			std::cout << "timed out waiting for changes\n";
		}
	}

	co_return 0;
}

int main(int argc, char** argv)
{

	int main_return;
	boost::asio::io_context io;

	boost::asio::co_spawn(io, co_main(argc, argv, io), [&](std::exception_ptr e, int ret) {
		if (e)
			std::rethrow_exception(e);
		main_return = ret;
		io.stop();
		});
	io.run();
	return main_return;
}
