
#ifdef _WIN32
#include <windows.h>
#endif

#include <string_view>
#include <string>
#include <stdexcept>
#include <map>

#include "httpd/http_misc_helper.hpp"
namespace httpd {

	static std::map<std::string_view, std::string> mime_map = {
		{ ".html", "text/html; charset=utf-8" },
		{ ".js", "application/javascript" },
		{ ".css", "text/css" },
		{ ".woff", "application/x-font-woff" },
		{ ".png", "image/png" },
		{ ".svg", "image/svg+xml" },
		{ ".md", "text/markdown" },
		{ ".jpg", "image/jpeg" },
		{ ".jpeg", "image/jpeg" },
		{ ".mp4", "video/mp4" },
		{ ".m4v", "video/mp4" },
		{ ".mkv", "video/x-matroska" },
		{ ".flv", "video/x-flv" },
		{ ".webp", "images/webp" }
	};

    std::string get_mime_type_from_extension(std::string_view extension)
    {
        if (mime_map.contains(extension))
            return mime_map.at(extension);
        return std::string{};
    }

	time_t dos2unixtime(unsigned long dostime)
	{
		struct tm ltime = { };

		ltime.tm_year = (dostime >> 25) + 80;
		ltime.tm_mon = ((dostime >> 21) & 0x0f) - 1;
		ltime.tm_mday = ((dostime >> 16) & 0x1f) +1;
		ltime.tm_hour = (dostime >> 11) & 0x0f;
		ltime.tm_min = (dostime >> 5) & 0x3f;
		ltime.tm_sec = (dostime & 0x1f) << 1;

		ltime.tm_wday = -1;
		ltime.tm_yday = -1;
		ltime.tm_isdst = -1;
		return std::mktime(&ltime);
	}

	std::string decodeURIComponent(std::string_view str)
	{
		std::string result;

		auto start = str.cbegin();
		auto end = str.cend();

		for (; start != end; ++start)
		{
			char c = *start;
			if (c == '%')
			{
				auto first = std::next(start);
				if (first == end)
					throw std::invalid_argument("URI malformed");

				auto second = std::next(first);
				if (second == end)
					throw std::invalid_argument("URI malformed");

				if (isdigit(*first))
					c = *first - '0';
				else if (*first >= 'A' && *first <= 'F')
					c = *first - 'A' + 10;
				else if (*first >= 'a' && *first <= 'f')
					c = *first - 'a' + 10;
				else
					throw std::invalid_argument("URI malformed");

				c <<= 4;

				if (isdigit(*second))
					c += *second - '0';
				else if (*second >= 'A' && *second <= 'F')
					c += *second - 'A' + 10;
				else if (*second >= 'a' && *second <= 'f')
					c += *second - 'a' + 10;
				else
					throw std::invalid_argument("URI malformed");

				std::advance(start, 2);
			}

			result += c;
		}

		return result;
	}

	// this is a clone of 'struct tm' but with all fields we don't need or use cut out.


}
