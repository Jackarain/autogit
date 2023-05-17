//
// notify_event.hpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2023 Jack (jack.arain at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

#pragma once

#include <boost/filesystem.hpp>
#include <vector>

namespace watchman {
	namespace fs = boost::filesystem;

	enum class event_type
	{
		unknown,
		creation,
		deletion,
		modification,
		rename,
	};

	struct notify_event
	{
		event_type type_;
		fs::path path_;
		fs::path new_path_;
	};

	using notify_events = std::vector<notify_event>;
}
