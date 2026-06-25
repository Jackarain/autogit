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

#include <deque>
#include <boost/filesystem.hpp>

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

	inline const char* to_string(event_type type) noexcept
	{
		switch (type)
		{
		case event_type::unknown:      return "unknown";
		case event_type::creation:     return "creation";
		case event_type::deletion:     return "deletion";
		case event_type::modification: return "modification";
		case event_type::rename:       return "rename";
		default:                       return "unknown";
		}
	}

	struct notify_event
	{
		event_type type_;
		fs::path path_;
		fs::path new_path_;
	};

	using notify_events = std::deque<notify_event>;
}
