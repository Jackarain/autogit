//
// watchman.hpp
// ~~~~~~~~~~~~
//
// Copyright (c) 2023 Jack (jack.arain at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

#pragma once

#include <boost/predef.h>

#if BOOST_OS_WINDOWS
# include "watchman/windows/windows_watchman.hpp"
namespace watchman {
	using watcher = watchman::windows_watch;
}
#elif BOOST_OS_LINUX
# include "watchman/linux/linux_watchman.hpp"
namespace watchman {
	using watcher = watchman::linux_watch;
}
#elif BOOST_OS_MACOS
# include "watchman/macos/macos_watchman.hpp"
namespace watchman {
	using watcher = watchman::macos_watch;
}
#else
# error There is no implementation for the platform yet.
#endif
