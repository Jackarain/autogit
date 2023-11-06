﻿/**
 ******************************************************************************
 *
 *  @mainpage strutil v1.0.2 - header-only string utility library documentation
 *  @see https://github.com/Shot511/strutil
 *
 *  @copyright  Copyright (C) 2022 Tomasz Galaj (Shot511)
 *  @file       strutil.hpp
 *  @brief      Library public interface header
 *
 *  @subsection Thank you all for your contributions!!
 *
 ******************************************************************************
*/

#ifndef INCLUDE__2023_10_18__STRUTIL_HPP
#define INCLUDE__2023_10_18__STRUTIL_HPP


#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <optional>

#include <boost/date_time/posix_time/posix_time.hpp>

//! The strutil namespace
namespace strutil
{
	/**
	 * @brief Converts any datatype into std::string.
	 *        Datatype must support << operator.
	 * @tparam T
	 * @param value - will be converted into std::string.
	 * @return Converted value as std::string.
	 */
	template<typename T>
	static inline std::string to_string(T value)
	{
		std::stringstream ss;
		ss << value;

		return ss.str();
	}

	/**
	 * @brief Converts std::string into any datatype.
	 *        Datatype must support << operator.
	 * @tparam T
	 * @param str - std::string that will be converted into datatype T.
	 * @return Variable of datatype T.
	 */
	template<typename T>
	static inline T parse_string(const std::string& str)
	{
		T result;
		std::istringstream(str) >> result;

		return result;
	}

	/**
	 * @brief Converts std::string to lower case.
	 * @param str - std::string that needs to be converted.
	 * @return Lower case input std::string.
	 */
	static inline std::string to_lower(const std::string& str)
	{
		auto result = str;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) -> unsigned char
			{
				return static_cast<unsigned char>(std::tolower(c));
			});

		return result;
	}

	/**
	 * @brief Converts std::string to upper case.
	 * @param str - std::string that needs to be converted.
	 * @return Upper case input std::string.
	 */
	static inline std::string to_upper(const std::string& str)
	{
		auto result = str;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) -> unsigned char
			{
				return static_cast<unsigned char>(std::toupper(c));
			});

		return result;
	}

	/**
	 * @brief Converts the first character of a string to uppercase letter and lowercases all other characters, if any.
	 * @param str - input string to be capitalized.
	 * @return A string with the first letter capitalized and all other characters lowercased. It doesn't modify the input string.
	 */
	static inline std::string capitalize(const std::string& str)
	{
		auto result = str;
		if (!result.empty())
		{
			result.front() = static_cast<char>(std::toupper(result.front()));
		}

		return result;
	}

	/**
	 * @brief Converts only the first character of a string to uppercase letter, all other characters stay unchanged.
	 * @param str - input string to be modified.
	 * @return A string with the first letter capitalized. All other characters stay unchanged. It doesn't modify the input string.
	 */
	static inline std::string capitalize_first_char(const std::string& str)
	{
		auto result = to_lower(str);
		if (!result.empty())
		{
			result.front() = static_cast<char>(std::toupper(result.front()));
		}

		return result;
	}

	/**
	 * @brief Checks if input std::string str contains specified substring.
	 * @param str - std::string to be checked.
	 * @param substring - searched substring.
	 * @return True if substring was found in str, false otherwise.
	 */
	static inline bool contains(const std::string& str, const std::string& substring)
	{
		return str.find(substring) != std::string::npos;
	}

	/**
	 * @brief Checks if input std::string str contains specified character.
	 * @param str - std::string to be checked.
	 * @param character - searched character.
	 * @return True if character was found in str, false otherwise.
	 */
	static inline bool contains(const std::string& str, const char character)
	{
		return contains(str, std::string(1, character));
	}

	/**
	 * @brief Compares two std::strings ignoring their case (lower/upper).
	 * @param str1 - std::string to compare
	 * @param str2 - std::string to compare
	 * @return True if str1 and str2 are equal, false otherwise.
	 */
	static inline bool compare_ignore_case(const std::string& str1, const std::string& str2)
	{
		return to_lower(str1) == to_lower(str2);
	}

	/**
	 * @brief Trims (in-place) white spaces from the left side of std::string.
	 *        Taken from: http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring.
	 * @param str - input std::string to remove white spaces from.
	 */
	static inline void trim_left(std::string& str)
	{
		str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](int ch) { return !std::isspace(ch); }));
	}

	/**
	 * @brief Trims (in-place) white spaces from the right side of std::string.
	 *        Taken from: http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring.
	 * @param str - input std::string to remove white spaces from.
	 */
	static inline void trim_right(std::string& str)
	{
		str.erase(std::find_if(str.rbegin(), str.rend(), [](int ch) { return !std::isspace(ch); }).base(), str.end());
	}

	/**
	 * @brief Trims (in-place) white spaces from the both sides of std::string.
	 *        Taken from: http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring.
	 * @param str - input std::string to remove white spaces from.
	 */
	static inline void trim(std::string& str)
	{
		trim_left(str);
		trim_right(str);
	}

	/**
	 * @brief Trims white spaces from the left side of std::string.
	 *        Taken from: http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring.
	 * @param str - input std::string to remove white spaces from.
	 * @return Copy of input str with trimmed white spaces.
	 */
	static inline std::string trim_left_copy(std::string str)
	{
		trim_left(str);
		return str;
	}

	/**
	  * @brief Trims white spaces from the right side of std::string.
	  *        Taken from: http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring.
	  * @param str - input std::string to remove white spaces from.
	  * @return Copy of input str with trimmed white spaces.
	  */
	static inline std::string trim_right_copy(std::string str)
	{
		trim_right(str);
		return str;
	}

	/**
	  * @brief Trims white spaces from the both sides of std::string.
	  *        Taken from: http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring.
	  * @param str - input std::string to remove white spaces from.
	  * @return Copy of input str with trimmed white spaces.
	  */
	static inline std::string trim_copy(std::string str)
	{
		trim(str);
		return str;
	}

	static inline std::string remove_spaces(std::string str)
	{
		str.erase(std::remove_if(
			str.begin(), str.end(), [](auto c) { return std::isspace(c); }),
			str.end());
		return str;
	}

	static inline std::wstring remove_spaces(std::wstring str)
	{
		str.erase(std::remove_if(
			str.begin(), str.end(), [](auto c) { return std::isspace(c); }),
			str.end());
		return str;
	}

	/**
	 * @brief Replaces (in-place) the first occurance of target with replacement.
	 *        Taken from: http://stackoverflow.com/questions/3418231/c-replace-part-of-a-string-with-another-string.
	 * @param str - input std::string that will be modified.
	 * @param target - substring that will be replaced with replacement.
	 * @param replacement - substring that will replace target.
	 * @return True if replacement was successfull, false otherwise.
	 */
	static inline bool replace_first(std::string& str, const std::string& target, const std::string& replacement)
	{
		const size_t start_pos = str.find(target);
		if (start_pos == std::string::npos)
		{
			return false;
		}

		str.replace(start_pos, target.length(), replacement);
		return true;
	}

	/**
	 * @brief Replaces (in-place) last occurance of target with replacement.
	 *        Taken from: http://stackoverflow.com/questions/3418231/c-replace-part-of-a-string-with-another-string.
	 * @param str - input std::string that will be modified.
	 * @param target - substring that will be replaced with replacement.
	 * @param replacement - substring that will replace target.
	 * @return True if replacement was successfull, false otherwise.
	 */
	static inline bool replace_last(std::string& str, const std::string& target, const std::string& replacement)
	{
		size_t start_pos = str.rfind(target);
		if (start_pos == std::string::npos)
		{
			return false;
		}

		str.replace(start_pos, target.length(), replacement);
		return true;
	}

	/**
	 * @brief Replaces (in-place) all occurances of target with replacement.
	 *        Taken from: http://stackoverflow.com/questions/3418231/c-replace-part-of-a-string-with-another-string.
	 * @param str - input std::string that will be modified.
	 * @param target - substring that will be replaced with replacement.
	 * @param replacement - substring that will replace target.
	 * @return True if replacement was successfull, false otherwise.
	 */
	static inline bool replace_all(std::string& str, const std::string& target, const std::string& replacement)
	{
		if (target.empty())
		{
			return false;
		}

		size_t start_pos = 0;
		const bool found_substring = str.find(target, start_pos) != std::string::npos;

		while ((start_pos = str.find(target, start_pos)) != std::string::npos)
		{
			str.replace(start_pos, target.length(), replacement);
			start_pos += replacement.length();
		}

		return found_substring;
	}

	static inline bool replace_all(std::wstring& str, const std::wstring& target, const std::wstring& replacement)
	{
		if (target.empty())
		{
			return false;
		}

		size_t start_pos = 0;
		const bool found_substring = str.find(target, start_pos) != std::wstring::npos;

		while ((start_pos = str.find(target, start_pos)) != std::wstring::npos)
		{
			str.replace(start_pos, target.length(), replacement);
			start_pos += replacement.length();
		}

		return found_substring;
	}

	/**
	 * @brief Checks if std::string str ends with specified suffix.
	 * @param str - input std::string that will be checked.
	 * @param suffix - searched suffix in str.
	 * @return True if suffix was found, false otherwise.
	 */
	static inline bool ends_with(const std::string& str, const std::string& suffix)
	{
		const auto suffix_start = str.size() - suffix.size();
		const auto result = str.find(suffix, suffix_start);
		return (result == suffix_start) && (result != std::string::npos);
	}

	static inline bool ends_with(const std::wstring& str, const std::wstring& suffix)
	{
		const auto suffix_start = str.size() - suffix.size();
		const auto result = str.find(suffix, suffix_start);
		return (result == suffix_start) && (result != std::wstring::npos);
	}

	/**
	 * @brief Checks if std::string str ends with specified character.
	 * @param str - input std::string that will be checked.
	 * @param suffix - searched character in str.
	 * @return True if ends with character, false otherwise.
	 */
	static inline bool ends_with(const std::string& str, const char suffix)
	{
		return !str.empty() && (str.back() == suffix);
	}

	static inline bool ends_with(const std::wstring& str, const wchar_t suffix)
	{
		return !str.empty() && (str.back() == suffix);
	}

	/**
	 * @brief Checks if std::string str starts with specified prefix.
	 * @param str - input std::string that will be checked.
	 * @param prefix - searched prefix in str.
	 * @return True if prefix was found, false otherwise.
	 */
	static inline bool starts_with(const std::string& str, const std::string& prefix)
	{
		return str.rfind(prefix, 0) == 0;
	}

	/**
	 * @brief Checks if std::string str starts with specified character.
	 * @param str - input std::string that will be checked.
	 * @param prefix - searched character in str.
	 * @return True if starts with character, false otherwise.
	 */
	static inline bool starts_with(const std::string& str, const char prefix)
	{
		return !str.empty() && (str.front() == prefix);
	}

	/**
	 * @brief Splits input std::string str according to input delim.
	 * @param str - std::string that will be splitted.
	 * @param delim - the delimiter.
	 * @return std::vector<std::string> that contains all splitted tokens.
	 */
	static inline std::vector<std::string> split(const std::string& str, const char delim)
	{
		std::vector<std::string> tokens;
		std::stringstream ss(str);

		std::string token;
		while (std::getline(ss, token, delim))
		{
			tokens.push_back(token);
		}

		// Match semantics of split(str,str)
		if (str.empty() || ends_with(str, delim)) {
			tokens.emplace_back();
		}

		return tokens;
	}

	/**
	 * @brief Splits input std::string str according to input std::string delim.
	 *        Taken from: https://stackoverflow.com/a/46931770/1892346.
	 * @param str - std::string that will be split.
	 * @param delim - the delimiter.
	 * @return std::vector<std::string> that contains all splitted tokens.
	 */
	static inline std::vector<std::string> split(const std::string& str, const std::string& delim)
	{
		size_t pos_start = 0, pos_end, delim_len = delim.length();
		std::string token;
		std::vector<std::string> tokens;

		while ((pos_end = str.find(delim, pos_start)) != std::string::npos)
		{
			token = str.substr(pos_start, pos_end - pos_start);
			pos_start = pos_end + delim_len;
			tokens.push_back(token);
		}

		tokens.push_back(str.substr(pos_start));
		return tokens;
	}

	/**
	 * @brief Splits input string using regex as a delimiter.
	 * @param src - std::string that will be split.
	 * @param rgx_str - the set of delimiter characters.
	 * @return vector of resulting tokens.
	 */
	static inline std::vector<std::string> regex_split(const std::string& src, const std::string& rgx_str)
	{
		std::vector<std::string> elems;
		const std::regex rgx(rgx_str);
		std::sregex_token_iterator iter(src.begin(), src.end(), rgx, -1);
		std::sregex_token_iterator end;
		while (iter != end)
		{
			elems.push_back(*iter);
			++iter;
		}
		return elems;
	}

	/**
	 * @brief Splits input string using regex as a delimiter.
	 * @param src - std::string that will be split.
	 * @param dest - map of matched delimiter and those being splitted.
	 * @param rgx_str - the set of delimiter characters.
	 * @return True if the parsing is successfully done.
	 */
	static inline std::map<std::string, std::string> regex_split_map(const std::string& src, const std::string& rgx_str)
	{
		std::map<std::string, std::string> dest;
		std::string tstr = src + " ";
		std::regex rgx(rgx_str);
		std::sregex_token_iterator niter(tstr.begin(), tstr.end(), rgx);
		std::sregex_token_iterator viter(tstr.begin(), tstr.end(), rgx, -1);
		std::sregex_token_iterator end;
		++viter;
		while (niter != end)
		{
			dest[*niter] = *viter;
			++niter;
			++viter;
		}

		return dest;
	}

	/**
	 * @brief Splits input string using any delimiter in the given set.
	 * @param str - std::string that will be split.
	 * @param delims - the set of delimiter characters.
	 * @return vector of resulting tokens.
	 */
	static inline std::vector<std::string> split_any(const std::string& str, const std::string& delims)
	{
		std::string token;
		std::vector<std::string> tokens;

		size_t pos_start = 0;
		for (size_t pos_end = 0; pos_end < str.length(); ++pos_end)
		{
			if (contains(delims, str[pos_end]))
			{
				token = str.substr(pos_start, pos_end - pos_start);
				tokens.push_back(token);
				pos_start = pos_end + 1;
			}
		}

		tokens.push_back(str.substr(pos_start));
		return tokens;
	}

	/**
	 * @brief Joins all elements of std::vector tokens of arbitrary datatypes
	 *        into one std::string with delimiter delim.
	 * @tparam T - arbitrary datatype.
	 * @param tokens - vector of tokens.
	 * @param delim - the delimiter.
	 * @return std::string with joined elements of vector tokens with delimiter delim.
	 */
	template<typename T>
	static inline std::string join(const std::vector<T>& tokens, const std::string& delim)
	{
		std::ostringstream result;
		for (auto it = tokens.begin(); it != tokens.end(); ++it)
		{
			if (it != tokens.begin())
			{
				result << delim;
			}

			result << *it;
		}

		return result.str();
	}

	/**
	 * @brief Inplace removal of all empty strings in a vector<string>
	 * @param tokens - vector of strings.
	 */
	static inline void drop_empty(std::vector<std::string>& tokens)
	{
		auto last = std::remove_if(tokens.begin(), tokens.end(), [](const std::string& s) { return s.empty(); });
		tokens.erase(last, tokens.end());
	}

	/**
	 * @brief Inplace removal of all empty strings in a vector<string>
	 * @param tokens - vector of strings.
	 * @return vector of non-empty tokens.
	 */
	static inline std::vector<std::string> drop_empty_copy(std::vector<std::string> tokens)
	{
		drop_empty(tokens);
		return tokens;
	}

	/**
	 * @brief Inplace removal of all duplicate strings in a vector<string> where order is not to be maintained
	 *        Taken from: C++ Primer V5
	 * @param tokens - vector of strings.
	 * @return vector of non-duplicate tokens.
	 */
	static inline void drop_duplicate(std::vector<std::string>& tokens)
	{
		std::sort(tokens.begin(), tokens.end());
		auto end_unique = std::unique(tokens.begin(), tokens.end());
		tokens.erase(end_unique, tokens.end());
	}

	/**
	 * @brief Removal of all duplicate strings in a vector<string> where order is not to be maintained
	 *        Taken from: C++ Primer V5
	 * @param tokens - vector of strings.
	 * @return vector of non-duplicate tokens.
	 */
	static inline std::vector<std::string> drop_duplicate_copy(std::vector<std::string> tokens)
	{
		std::sort(tokens.begin(), tokens.end());
		auto end_unique = std::unique(tokens.begin(), tokens.end());
		tokens.erase(end_unique, tokens.end());
		return tokens;
	}

	/**
	 * @brief Creates new std::string with repeated n times substring str.
	 * @param str - substring that needs to be repeated.
	 * @param n - number of iterations.
	 * @return std::string with repeated substring str.
	 */
	static inline std::string repeat(const std::string& str, unsigned n)
	{
		std::string result;

		for (unsigned i = 0; i < n; ++i)
		{
			result += str;
		}

		return result;
	}

	/**
	 * @brief Creates new std::string with repeated n times char c.
	 * @param c - char that needs to be repeated.
	 * @param n - number of iterations.
	 * @return std::string with repeated char c.
	 */
	static inline std::string repeat(char c, unsigned n)
	{
		return std::string(n, c);
	}

	/**
	 * @brief Checks if input std::string str matches specified reular expression regex.
	 * @param str - std::string to be checked.
	 * @param regex - the std::regex regular expression.
	 * @return True if regex matches str, false otherwise.
	 */
	static inline bool matches(const std::string& str, const std::regex& regex)
	{
		return std::regex_match(str, regex);
	}

	/**
	 * @brief Sort input std::vector<std::string> strs in ascending order.
	 * @param strs - std::vector<std::string> to be checked.
	 */
	template<typename T>
	static inline void sorting_ascending(std::vector<T>& strs)
	{
		std::sort(strs.begin(), strs.end());
	}

	/**
	 * @brief Sorted input std::vector<std::string> strs in descending order.
	 * @param strs - std::vector<std::string> to be checked.
	 */
	template<typename T>
	static inline void sorting_descending(std::vector<T>& strs)
	{
		std::sort(strs.begin(), strs.end(), std::greater<T>());
	}

	/**
	 * @brief Reverse input std::vector<std::string> strs.
	 * @param strs - std::vector<std::string> to be checked.
	 */
	template<typename T>
	static inline void reverse_inplace(std::vector<T>& strs)
	{
		std::reverse(strs.begin(), strs.end());
	}

	/**
	 * @brief Reverse input std::vector<std::string> strs.
	 * @param strs - std::vector<std::string> to be checked.
	 */
	template<typename T>
	static inline std::vector<T> reverse_copy(std::vector<T> strs)
	{
		std::reverse(strs.begin(), strs.end());
		return strs;
	}

	constexpr int isalpha(const int& c)
	{
		return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ? 1 : 0);
	}

	constexpr int isdigit(const int& c)
	{
		return (c >= '0' && c <= '9' ? 1 : 0);
	}

	constexpr int isalnum(const int& c)
	{
		return (isalpha(c) || isdigit(c));
	}


	//////////////////////////////////////////////////////////////////////////

	/**
	 * @brief Unescape url string.
	 * @param in - input string.
	 * @param out - output string.
	 * @return True if successfully, false otherwise.
	 */
	static inline bool unescape(std::string_view in, std::string& out)
	{
		out.clear();
		out.reserve(in.size());

		for (std::size_t i = 0; i < in.size(); ++i)
		{
			switch (in[i])
			{
			case '%':
				if (i + 3 <= in.size())
				{
					unsigned int value = 0;
					for (std::size_t j = i + 1; j < i + 3; ++j)
					{
						switch (in[j])
						{
						case '0': case '1':
						case '2': case '3':
						case '4': case '5':
						case '6': case '7':
						case '8': case '9':
							value += in[j] - '0';
							break;
						case 'a': case 'b':
						case 'c': case 'd':
						case 'e': case 'f':
							value += in[j] - 'a' + 10;
							break;
						case 'A': case 'B':
						case 'C': case 'D':
						case 'E': case 'F':
							value += in[j] - 'A' + 10;
							break;
						default:
							return false;
						}
						if (j == i + 1)
							value <<= 4;
					}
					out += static_cast<char>(value);
					i += 2;

					continue;
				}
				return false;
				break;
			case '+':
				out += ' ';
				break;
			case '-': case '_':
			case '.': case '!':
			case '~': case '*':
			case '\'': case '(':
			case ')': case ':':
			case '@': case '&':
			case '=': case '$':
			case ',': case '/':
			case ';':
				out += in[i];
				break;
			default:
				// if (!isalnum((unsigned char)in[i]))
				//   return false;
				out += in[i];
				break;
			}
		}

		return true;
	}

	static inline std::string to_string(float v, int width, int precision = 3)
	{
		char buf[20] = { 0 };
		std::snprintf(buf, 20, "%*.*f", width, precision, v);
		return std::string(buf);
	}

	static inline std::string add_suffix(float val, char const* suffix = nullptr)
	{
		std::string ret;

		const char* prefix[] = { "kB", "MB", "GB", "TB" };
		for (auto& i : prefix)
		{
			val /= 1024.f;
			if (std::fabs(val) < 1024.f)
			{
				ret = to_string(val, 4);
				ret += i;
				if (suffix) ret += suffix;
				return ret;
			}
		}
		ret = to_string(val, 4);
		ret += "PB";
		if (suffix) ret += suffix;
		return ret;
	}

	static inline bool is_space(const char c)
	{
		if (c == ' ' ||
			c == '\f' ||
			c == '\n' ||
			c == '\r' ||
			c == '\t' ||
			c == '\v')
			return true;
		return false;
	}

	static inline std::string_view string_trim(std::string_view sv)
	{
		const char* b = sv.data();
		const char* e = b + sv.size();

		for (; b != e; b++)
		{
			if (!is_space(*b))
				break;
		}

		for (; e != b; )
		{
			if (!is_space(*(--e)))
			{
				++e;
				break;
			}
		}

		return std::string_view(b, e - b);
	}

	static inline std::string_view string_trim_left(std::string_view sv)
	{
		const char* b = sv.data();
		const char* e = b + sv.size();

		for (; b != e; b++)
		{
			if (!is_space(*b))
				break;
		}

		return std::string_view(b, e - b);
	}

	static inline std::string_view string_trim_right(std::string_view sv)
	{
		const char* b = sv.data();
		const char* e = b + sv.size();

		for (; e != b; )
		{
			if (!is_space(*(--e)))
			{
				++e;
				break;
			}
		}

		return std::string_view(b, e - b);
	}

	template <class Iterator>
	std::string to_hex(Iterator it, Iterator end, std::string const& prefix)
	{
		using traits = std::iterator_traits<Iterator>;
		static_assert(sizeof(typename traits::value_type) == 1, "to_hex needs byte-sized element type");

		static char const* hexdigits = "0123456789abcdef";
		size_t off = prefix.size();
		std::string hex(std::distance(it, end) * 2 + off, '0');
		hex.replace(0, off, prefix);
		for (; it != end; it++)
		{
			hex[off++] = hexdigits[(*it >> 4) & 0x0f];
			hex[off++] = hexdigits[*it & 0x0f];
		}

		return hex;
	}

	static inline std::string to_hex(std::string_view data)
	{
		return to_hex(data.begin(), data.end(), "");
	}

	static inline std::string to_hex_prefixed(std::string_view data)
	{
		return to_hex(data.begin(), data.end(), "0x");
	}

	static inline char from_hex_char(char c) noexcept
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	static inline bool from_hexstring(std::string_view src, std::vector<uint8_t>& result)
	{
		unsigned s = (src.size() >= 2 && src[0] == '0' && src[1] == 'x') ? 2 : 0;
		result.reserve((src.size() - s + 1) / 2);

		if (src.size() % 2)
		{
			auto h = from_hex_char(src[s++]);
			if (h != static_cast<char>(-1))
				result.push_back(h);
			else
				return false;
		}
		for (unsigned i = s; i < src.size(); i += 2)
		{
			int h = from_hex_char(src[i]);
			int l = from_hex_char(src[i + 1]);

			if (h != -1 && l != -1)
			{
				result.push_back((uint8_t)(h * 16 + l));
				continue;
			}
			return false;
		}

		return true;
	}

	static inline bool is_hexstring(std::string const& src) noexcept
	{
		auto it = src.begin();
		if (src.compare(0, 2, "0x") == 0)
			it += 2;
		return std::all_of(it, src.end(),
			[](char c) { return from_hex_char(c) != static_cast<char>(-1); });
	}

	static inline std::string to_string(std::vector<uint8_t> const& data)
	{
		return std::string(
			(char const*)data.data(), (char const*)(data.data() + data.size()));
	}

	static inline std::string to_string(const boost::posix_time::ptime& t)
	{
		if (t.is_not_a_date_time())
			return "";

		return boost::posix_time::to_iso_extended_string(t);
	}

	static inline bool valid_utf(unsigned char* string, int length)
	{
		static const unsigned char utf8_table[] =
		{
		  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
		  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
		  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
		  3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
		};

		unsigned char* p;

		if (length < 0)
		{
			for (p = string; *p != 0; p++);
			length = (int)(p - string);
		}

		for (p = string; length-- > 0; p++)
		{
			unsigned char ab, c, d;

			c = *p;
			if (c < 128) continue;                /* ASCII character */

			if (c < 0xc0)                         /* Isolated 10xx xxxx byte */
				return false;

			if (c >= 0xfe)                        /* Invalid 0xfe or 0xff bytes */
				return false;

			ab = utf8_table[c & 0x3f];            /* Number of additional bytes */
			if (length < ab)
				return false;
			length -= ab;                         /* Length remaining */

			/* Check top bits in the second byte */
			if (((d = *(++p)) & 0xc0) != 0x80)
				return false;

			/* For each length, check that the remaining bytes start with the 0x80 bit
			   set and not the 0x40 bit. Then check for an overlong sequence, and for the
			   excluded range 0xd800 to 0xdfff. */
			switch (ab)
			{
				/* 2-byte character. No further bytes to check for 0x80. Check first byte
				   for for xx00 000x (overlong sequence). */
			case 1:
				if ((c & 0x3e) == 0)
					return false;
				break;
			case 2:
				if ((*(++p) & 0xc0) != 0x80)     /* Third byte */
					return false;
				if (c == 0xe0 && (d & 0x20) == 0)
					return false;
				if (c == 0xed && d >= 0xa0)
					return false;
				break;

				/* 4-byte character. Check 3rd and 4th bytes for 0x80. Then check first 2
				   bytes for for 1111 0000, xx00 xxxx (overlong sequence), then check for a
				   character greater than 0x0010ffff (f4 8f bf bf) */
			case 3:
				if ((*(++p) & 0xc0) != 0x80)     /* Third byte */
					return false;
				if ((*(++p) & 0xc0) != 0x80)     /* Fourth byte */
					return false;
				if (c == 0xf0 && (d & 0x30) == 0)
					return false;
				if (c > 0xf4 || (c == 0xf4 && d > 0x8f))
					return false;
				break;

				/* 5-byte and 6-byte characters are not allowed by RFC 3629, and will be
				   rejected by the length test below. However, we do the appropriate tests
				   here so that overlong sequences get diagnosed, and also in case there is
				   ever an option for handling these larger code points. */

				   /* 5-byte character. Check 3rd, 4th, and 5th bytes for 0x80. Then check for
					  1111 1000, xx00 0xxx */
			case 4:
				if ((*(++p) & 0xc0) != 0x80)     /* Third byte */
					return false;
				if ((*(++p) & 0xc0) != 0x80)     /* Fourth byte */
					return false;
				if ((*(++p) & 0xc0) != 0x80)     /* Fifth byte */
					return false;
				if (c == 0xf8 && (d & 0x38) == 0)
					return false;
				break;

				/* 6-byte character. Check 3rd-6th bytes for 0x80. Then check for
				   1111 1100, xx00 00xx. */
			case 5:
				if ((*(++p) & 0xc0) != 0x80)     /* Third byte */
					return false;
				if ((*(++p) & 0xc0) != 0x80)     /* Fourth byte */
					return false;
				if ((*(++p) & 0xc0) != 0x80)     /* Fifth byte */
					return false;
				if ((*(++p) & 0xc0) != 0x80)     /* Sixth byte */
					return false;
				if (c == 0xfc && (d & 0x3c) == 0)
					return false;
				break;
			}

			/* Character is valid under RFC 2279, but 4-byte and 5-byte characters are
			   excluded by RFC 3629. The pointer p is currently at the last byte of the
			   character. */
			if (ab > 3)
				return false;
		}

		return true;
	}

	static inline std::optional<std::u16string> utf8_utf16(std::u8string_view utf8)
	{
		const char8_t* first = &utf8[0];
		const char8_t* last = first + utf8.size();

		std::u16string result(utf8.size(), char16_t{ 0 });
		char16_t* dest = &result[0];
		char16_t* next = nullptr;

		using codecvt_type = std::codecvt<char16_t, char8_t, std::mbstate_t>;

		codecvt_type* cvt = new codecvt_type;

		// manages reference to codecvt facet to free memory.
		std::locale loc;
		loc = std::locale(loc, cvt);

		codecvt_type::state_type state{};

		auto ret = cvt->in(
			state, first, last, first, dest, dest + result.size(), next);
		if (ret != codecvt_type::ok)
			return {};

		result.resize(static_cast<size_t>(next - dest));
		return result;
	}

	static inline std::optional<std::u8string> utf16_utf8(std::u16string_view utf16)
	{
		const char16_t* first = &utf16[0];
		const char16_t* last = first + utf16.size();

		std::u8string result((utf16.size() + 1) * 6, char{ 0 });
		char8_t* dest = &result[0];
		char8_t* next = nullptr;

		using codecvt_type = std::codecvt<char16_t, char8_t, std::mbstate_t>;

		codecvt_type* cvt = new codecvt_type;
		// manages reference to codecvt facet to free memory.
		std::locale loc;
		loc = std::locale(loc, cvt);

		codecvt_type::state_type state{};

		auto ret = cvt->out(
			state, first, last, first, dest, dest + result.size(), next);
		if (ret != codecvt_type::ok)
			return {};

		result.resize(static_cast<size_t>(next - dest));
		return result;
	}

	static inline std::optional<std::wstring> string_wide(std::string_view src)
	{
		const char* first = src.data();
		const char* last = src.data() + src.size();
		const char* snext = nullptr;

		std::wstring result(src.size() + 1, wchar_t{ 0 });

		wchar_t* dest = result.data();
		wchar_t* dnext = nullptr;

		using codecvt_type = std::codecvt<wchar_t, char, mbstate_t>;
		std::locale sys_locale("");
		mbstate_t in_state;

		auto ret = std::use_facet<codecvt_type>(sys_locale).in(
			in_state, first, last, snext, dest, dest + result.size(), dnext);
		if (ret != codecvt_type::ok)
			return {};

		result.resize(static_cast<size_t>(dnext - dest));
		return result;
	}

	static inline std::optional<std::string> wide_string(std::wstring_view src)
	{
		const wchar_t* first = src.data();
		const wchar_t* last = src.data() + src.size();
		const wchar_t* snext = nullptr;

		std::string result((src.size() + 1) * 6, char(0));
		char* dest = &result[0];
		char* dnext = nullptr;

		using codecvt_type = std::codecvt<wchar_t, char, mbstate_t>;
		std::locale sys_locale("");
		mbstate_t out_state;

		auto ret = std::use_facet<codecvt_type>(sys_locale).out(
			out_state, first, last, snext,
			dest, dest + result.size(), dnext);

		if (ret != codecvt_type::ok)
			return {};

		result.resize(static_cast<size_t>(dnext - dest));
		return result;
	}


	//////////////////////////////////////////////////////////////////////////
	static inline constexpr char ascii_tolower(const char c) noexcept
	{
		return ((static_cast<unsigned>(c) - 65U) < 26) ?
			c + 'a' - 'A' : c;
	}

	static inline constexpr bool ishexdigit(const char c) noexcept
	{
		return isdigit(c) || ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
	}

	// 0xFFFF
	// 000008
	// 123456
	static inline std::optional<int64_t>
	from_string(std::string_view str, int base = -1)
	{
		const char* start = str.data();
#if 0
		const char* end = str.data() + str.size();

		if (start >= end)
			return {};

		bool has_prefix = false;

		if (*start == '0')
		{
			if (base == -1)
				base = 8;

			if (end - start >= 2 &&
				ascii_tolower(*(start + 1)) == 'x')
			{
				if (base == -1)
					base = 16;
				has_prefix = true;
			}
		}

		if (base == -1)
			base = 10;

		if (base == 16 && has_prefix)
			start += 2;

		const char* p = start;
		while (p < end)
		{
			const char c = *p++;
			switch (base)
			{
			case 8:
				if (c < '0' || c > '7')
					return {};
				continue;
			case 10:
				if (!isdigit(c))
					return {};
				continue;
			case 16:
				if (!ishexdigit(c))
					return {};
				continue;
			}
		}
#endif
		return std::strtoll(start, nullptr, base);
	}

	static inline bool is_ipv4_host(std::string_view str)
	{
		const char* b = str.data();
		const char* e = str.data() + str.size();
		int parts = 0;
		const char* start = b;
		int64_t last = 0;
		int64_t max = 0;

		while (b != e)
		{
			const char c = *b++;
			bool eol = b == e;

			if (c == '.' || eol)
			{
				if (++parts > 4)
					return false;

				const char* end = eol ? b : b - 1;
				auto ret = from_string({ start, static_cast<size_t>(end - start) });
				if (!ret)
					return false;

				last = *ret;
				if (last < 0)
					return false;

				if (max < last)
					max = last;

				start = b;
			}
		}

		if (parts == 0 || parts > 4)
		{
			if (str.size() == 0)
				return false;
			return false;
		}

		if (max > 255 && last < max)
			return false;

		last >>= (8 * (4 - (parts - 1)));
		if (last != 0)
			return false;

		return true;
	}

	inline bool is_ipv6_host(std::string_view str)
	{
		const char* b = str.data();
		const char* e = str.data() + str.size();
		const char* start = b;
		int parts = 0;
		int colons = 0;
		char last_char = '\0';
		uint16_t value[8];

		while (b != e)
		{
			const char c = *b++;
			bool eol = b == e;

			if (c == ':' || eol)
			{
				const char* end = eol ? b : b - 1;
				auto ret = from_string({ start, static_cast<size_t>(end - start) }, 16);
				if (!ret)
					return false;

				int64_t n = *ret;
				if (n > 0xffff)
					return false;

				value[parts] = static_cast<uint16_t>(n);
				parts++;
				start = b;

				if (last_char == ':' && last_char == c)
				{
					colons++;
					if (colons > 1)
						return false;
				}

				bool is_ipv4 = false;
				if (parts == 3 && colons == 1 && n == 0xffff) // ipv4
					is_ipv4 = true;

				if (parts == 6
					&& colons == 0
					&& (value[0] == 0 && value[1] == 0 && value[2] == 0
						&& value[3] == 0 && value[4] == 0)
					&& n == 0xffff) // ipv4
					is_ipv4 = true;

				if (is_ipv4)
				{
					if (!is_ipv4_host({ b, static_cast<size_t>(e - b) }))
						return false;

					return true;
				}
			}
			else
			{
				if (!ishexdigit(c))
					return false;
			}

			last_char = c;
		}

		if ((parts > 8) || (parts < 8 && colons == 0))
			return false;

		return true;
	}


	const inline std::string base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	inline bool is_base64(unsigned char c)
	{
		return (c == 43 || // +
			(c >= 47 && c <= 57) || // /-9
			(c >= 65 && c <= 90) || // A-Z
			(c >= 97 && c <= 122)); // a-z
	}

	inline std::string base64_encode(unsigned char const* input, size_t len)
	{
		std::string ret;
		int i = 0;
		int j = 0;
		unsigned char char_array_3[3];
		unsigned char char_array_4[4];

		while (len--) {
			char_array_3[i++] = *(input++);
			if (i == 3) {
				char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
				char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
					((char_array_3[1] & 0xf0) >> 4);
				char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
					((char_array_3[2] & 0xc0) >> 6);
				char_array_4[3] = char_array_3[2] & 0x3f;

				for (i = 0; (i < 4); i++) {
					ret += base64_chars[char_array_4[i]];
				}
				i = 0;
			}
		}

		if (i) {
			for (j = i; j < 3; j++) {
				char_array_3[j] = '\0';
			}

			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
				((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
				((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (j = 0; (j < i + 1); j++) {
				ret += base64_chars[char_array_4[j]];
			}

			while ((i++ < 3)) {
				ret += '=';
			}
		}

		return ret;
	}

	inline std::string base64_encode(std::string const& input)
	{
		return base64_encode(
			reinterpret_cast<const unsigned char*>(input.data()),
			input.size()
		);
	}

	inline std::string base64_decode(std::string const& input)
	{
		size_t in_len = input.size();
		int i = 0;
		int j = 0;
		int in_ = 0;
		unsigned char char_array_4[4], char_array_3[3];
		std::string ret;

		while (in_len-- && (input[in_] != '=') && is_base64(input[in_])) {
			char_array_4[i++] = input[in_]; in_++;
			if (i == 4) {
				for (i = 0; i < 4; i++) {
					char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
				}

				char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
				char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
				char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

				for (i = 0; (i < 3); i++) {
					ret += char_array_3[i];
				}
				i = 0;
			}
		}

		if (i) {
			for (j = i; j < 4; j++)
				char_array_4[j] = 0;

			for (j = 0; j < 4; j++)
				char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (j = 0; (j < i - 1); j++) {
				ret += static_cast<std::string::value_type>(char_array_3[j]);
			}
		}

		return ret;
	}

}

#endif // INCLUDE__2023_10_18__STRUTIL_HPP
