
#pragma once

#include <optional>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace httpd {

    struct BytesRange
    {
        std::uint64_t begin {0};
        std::uint64_t end {0};
    };

	bool parse_gmt_time_fmt(std::string_view date_str, time_t* output);

    std::optional<BytesRange> parse_range(std::string_view range);
    std::string make_http_last_modified(std::time_t t);
    std::string make_cpntent_range(BytesRange, std::uint64_t content_length);

    std::map<std::string, std::string> parse_cookie(std::string_view cookie_line);
}
