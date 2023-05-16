
#pragma once

#include <set>
#include <filesystem>

#include <boost/asio.hpp>
#include <boost/asio/windows/overlapped_handle.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/nowide/convert.hpp>
#include "win_dirchange_read_handle.hpp"
#include "../change_notify.hpp"

namespace dirmon {
	class windows_dirmon
	{
	public:
		template<typename ExecutionContext>
		windows_dirmon(ExecutionContext&& e, std::filesystem::path dirname)
			: m_dirhandle(std::forward<ExecutionContext>(e), dirname.string())
		{}

		void exclude(const std::string& str)
		{
			m_filters.insert(str);
		}

		boost::asio::awaitable<std::vector<dir_change_notify>> async_wait_dirchange()
		{
			std::vector<dir_change_notify> ret;
			std::array<char, 4096> buf;
			auto bytes_transferred = co_await m_dirhandle.async_read_some(boost::asio::buffer(buf), boost::asio::use_awaitable);

			if (bytes_transferred >= sizeof(FILE_NOTIFY_INFORMATION))
			{
				for (auto file_notify_info = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(buf.data());;
					file_notify_info = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(reinterpret_cast<char*>(file_notify_info) + file_notify_info->NextEntryOffset)
				)
				{
					dir_change_notify c;

					c.file_name = boost::nowide::narrow(file_notify_info->FileName, file_notify_info->FileNameLength/2);

					if (!filter(c.file_name.string()))
						ret.push_back(c);

					if (file_notify_info->NextEntryOffset ==0)
						break;
				}
			}

			co_return ret;
		}

		void close(boost::system::error_code ec)
		{
			m_dirhandle.close(ec);
		}

	private:
		bool filter(const std::string& str) const noexcept
		{
			for (const auto& p : m_filters)
			{
				if (str.starts_with(p))
					return true;
			}

			return false;
		}

	private:
		std::set<std::string> m_filters;
		detail::win_dirchange_read_handle m_dirhandle;
	};
}