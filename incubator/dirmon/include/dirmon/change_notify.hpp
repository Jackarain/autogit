
#pragma once

#include <filesystem>
#include <boost/asio.hpp>

namespace dirmon {

	enum dir_change_type_t
	{
		file_creation,
		file_deletion,
		file_modification,
		file_rename,
	};

	struct dir_change_notify
	{
		dir_change_type_t change_type;
		std::filesystem::path file_name;
	};
}
