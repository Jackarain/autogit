
#pragma once

#include <set>
#include <map>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/bimap.hpp>

#include "../change_notify.hpp"


#include <sys/inotify.h>
#include <iostream>

namespace dirmon {
	class inotify_dirmon
	{
	public:
		template<typename ExecutionContext>
		inotify_dirmon(ExecutionContext&& e, std::filesystem::path dirname)
			: m_inotify_handle(std::forward<ExecutionContext>(e), inotify_init1(IN_CLOEXEC|IN_NONBLOCK))
		{
			// scan for subdir and add it.
			add_subdirs(dirname);
        }

		void exclude(const std::string& str)
		{
			m_filters.insert(str);
		}

		void add_watch(std::filesystem::path dirname)
		{
			auto wd = inotify_add_watch(m_inotify_handle.native_handle(), dirname.string().c_str(), IN_CLOSE_WRITE|IN_MODIFY|IN_CREATE|IN_MOVE|IN_MASK_CREATE|IN_DELETE|IN_DELETE_SELF);
			if (wd >= 0){
				auto [it, ok] = wd_map.left.insert( { wd, dirname } );
				if (ok)
					std::cerr << "add watch for " << dirname << " as wd:" << wd << std::endl;
			}
		}

		void add_subdirs(std::filesystem::path dirname)
		{
			add_watch(dirname);
			for (const auto& entry : std::filesystem::directory_iterator(dirname))
			{
				if (entry.is_directory())
					add_subdirs(entry.path());
			}
		}

		boost::asio::awaitable<std::vector<dir_change_notify>> async_wait_dirchange()
		{
			std::vector<dir_change_notify> ret;
			std::array<char, 4096> buf;
			try {
			auto bytes_transferred = co_await m_inotify_handle.async_read_some(boost::asio::buffer(buf), boost::asio::use_awaitable);
            decltype(bytes_transferred) pos = 0;

			if (bytes_transferred >= sizeof(inotify_event))
			{
				for (inotify_event* file_notify_info = reinterpret_cast<inotify_event*>(buf.data());
					pos < bytes_transferred;
                    pos += sizeof(inotify_event) + file_notify_info->len,
					file_notify_info = reinterpret_cast<inotify_event*>(&buf[pos])
				)
				{
					if (file_notify_info->len > 0)
					{
						dir_change_notify c;
						std::string file_name = file_notify_info->name;
						c.file_name = std::filesystem::path(wd_map.left.find(file_notify_info->wd)->second) / file_name;

						if (filter(c.file_name.string()))
							continue;

						ret.push_back(c);

						auto mask = file_notify_info->mask;

						if (mask & IN_ISDIR)
						{
							if (mask & IN_CREATE)
							{
								add_watch(c.file_name);
							}
							else if (mask & IN_DELETE_SELF)
							{
								std::cerr << "wd self delete on " << file_notify_info->wd << " aka: " << c.file_name << std::endl;
								inotify_rm_watch(m_inotify_handle.native_handle(), file_notify_info->wd);
								wd_map.right.erase(c.file_name.string());
								std::cerr << "drop watch for wd: " << file_notify_info->wd << " aka :" << c.file_name << std::endl;
							}
							else if (mask & IN_DELETE)
							{
								std::cerr << "wd delete on " << file_notify_info->wd << " aka: " << c.file_name << std::endl;
								if (wd_map.right.find(c.file_name.string()) != wd_map.right.end())
								{
									auto wd = wd_map.right.find(c.file_name.string())->second;
									inotify_rm_watch(m_inotify_handle.native_handle(), wd);
									wd_map.right.erase(c.file_name.string());
									std::cerr << "drop watch for wd: " << wd << " aka :" << c.file_name << std::endl;
								}
							}
							else if (mask & IN_MOVED_FROM)
							{
								std::cerr << "rename from " << file_notify_info->wd << " aka: " << file_name << std::endl;
								auto it = wd_map.right.find(c.file_name.string());
								if ( it != wd_map.right.end())
								{
									wd_of_cookie[ file_notify_info->cookie ] = it->second;
								}
							}
							else if (mask & IN_MOVED_TO)
							{
								using namespace boost::bimaps;
								auto it = wd_map.left.find(wd_of_cookie[file_notify_info->cookie]);
								wd_map.left.replace_data(it, c.file_name);
								wd_of_cookie.erase(file_notify_info->cookie);
							}
						}
					}
				}
			}
			}catch(...)
			{
				// std::cerr << "async_wait_dirchange exceptioned\n";
			}

			co_return ret;
		}

		void close(boost::system::error_code ec)
		{
			m_inotify_handle.close(ec);
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
		boost::asio::posix::stream_descriptor m_inotify_handle;
		boost::bimap<int, std::string> wd_map;

		std::map<uint32_t, int> wd_of_cookie;
	};
}
