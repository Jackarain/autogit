
#pragma once

#include <filesystem>
#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/awaitable.hpp>

#include <fmt/format.h>
#include <iostream>

#include "../change_notify.hpp"

#include <sys/fanotify.h>

namespace dirmon {
	class fanotify_dirmon
	{
	public:
		template<typename ExecutionContext>
		fanotify_dirmon(ExecutionContext&& e, std::filesystem::path dirname)
			: m_fanotify_handle(std::forward<ExecutionContext>(e), fanotify_init(FAN_CLASS_NOTIF|FAN_CLOEXEC, O_RDONLY|O_CLOEXEC))
		{
            auto ret = fanotify_mark(m_fanotify_handle.native_handle(), FAN_MARK_ADD, FAN_CLOSE_WRITE|FAN_EVENT_ON_CHILD, AT_FDCWD, dirname.string().c_str());
			if (ret < 0)
			{
				perror("fanotify_dirmon");
				throw 0;
			}
        }

		boost::asio::awaitable<std::vector<dir_change_notify>> async_wait_dirchange()
		{
			std::vector<dir_change_notify> ret;
			std::array<char, 65536> buf;
			auto bytes_transferred = co_await m_fanotify_handle.async_read_some(boost::asio::buffer(buf), boost::asio::use_awaitable);
            decltype(bytes_transferred) pos = 0;

			std::cerr << "fanotify read returned size=" << bytes_transferred << "\n";

			if (bytes_transferred >= sizeof(fanotify_event_metadata))
			{
				for (fanotify_event_metadata* file_notify_info = reinterpret_cast<fanotify_event_metadata*>(buf.data());
				 	pos < bytes_transferred;
                    pos += sizeof(fanotify_event_metadata) + file_notify_info->event_len,
					file_notify_info = reinterpret_cast<fanotify_event_metadata*>(&buf[pos])
				)
				{
					if (file_notify_info->fd > 0)
					{
						dir_change_notify c;

						std::string proc_path = fmt::format("/proc/self/fd/{}", file_notify_info->fd);

						std::array<char, 4096> notify_filename_buf;
						auto filename_len = readlink(proc_path.c_str(), &notify_filename_buf[0], notify_filename_buf.size());
						::close(file_notify_info->fd);
						c.file_name = std::string(notify_filename_buf.data(), filename_len);
						ret.push_back(c);
					}
				}
			}

			co_return ret;
		}

		void close(boost::system::error_code ec)
		{
			m_fanotify_handle.close(ec);
		}

		boost::asio::posix::stream_descriptor m_fanotify_handle;
	};
}
