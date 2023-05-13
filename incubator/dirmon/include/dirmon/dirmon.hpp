
#pragma once

#include <boost/asio.hpp>

#if BOOST_OS_WINDOWS
#include "windows/windows_dirmon.hpp"
namespace dirmon {
	using dirmon = windows_dirmon;
}
#endif

#if BOOST_OS_LINUX
#include "linux/inotify_dirmon.hpp"
// #include "linux/fanotify_dirmon.hpp"
namespace dirmon {
	using dirmon = inotify_dirmon;
	// using dirmon = fanotify_dirmon;
}
#endif
