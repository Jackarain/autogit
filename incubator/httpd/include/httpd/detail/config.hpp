
#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace httpd
{

	constexpr bool has_so_reuseport()
	{
#ifdef SO_REUSEPORT
		return true;
#else
		return false;
#endif
	}

	namespace socket_options
	{
#ifdef SO_REUSEPORT
		typedef boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT> reuse_port;
#endif
	}
}
