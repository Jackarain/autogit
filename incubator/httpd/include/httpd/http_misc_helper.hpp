
#pragma once

#include <map>
#include <string>
#include <map>
#include <ctime>
#include <cstring>
#include <cctype>
#include <string_view>

namespace httpd {
	std::string get_mime_type_from_extension(std::string_view extension);

	time_t dos2unixtime(unsigned long dostime);
	std::string decodeURIComponent(std::string_view str);
}
