//
// windows_watchman.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2023 Jack (jack.arain at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/windows/overlapped_ptr.hpp>
#include <boost/asio/windows/overlapped_handle.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>

#include <boost/filesystem.hpp>
#include <boost/throw_exception.hpp>

#include <type_traits>
#include <utility>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "watchman/notify_event.hpp"


namespace watchman {

	namespace net = boost::asio;
	namespace fs = boost::filesystem;

	template <typename Executor = net::any_io_executor>
	class windows_watch_service : public net::windows::basic_overlapped_handle<Executor>
	{
	private:
		windows_watch_service(const windows_watch_service&) = delete;
		windows_watch_service& operator=(const windows_watch_service&) = delete;

	public:
		windows_watch_service(const Executor& ex, const fs::path& dir)
			: net::windows::basic_overlapped_handle<Executor>(
				ex,
				CreateFileW(dir.wstring().c_str(),
					FILE_LIST_DIRECTORY,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr,
					OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
					nullptr))
			, m_watch_dir(dir)
		{
		}

		windows_watch_service(const Executor& ex)
			: net::windows::basic_overlapped_handle<Executor>(ex)
		{}
		~windows_watch_service() = default;

		windows_watch_service(windows_watch_service&&) = default;
		windows_watch_service& operator=(windows_watch_service&&) = default;

		inline void open(const fs::path& dir, boost::system::error_code& ec)
		{
			m_watch_dir = dir;

			if (this->is_open())
				this->close();

			auto h = CreateFileW(dir.wstring().c_str(),
				FILE_LIST_DIRECTORY,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
				nullptr);

			this->assign(h, ec);
		}

		inline void open(const fs::path& dir)
		{
			boost::system::error_code ec;
			m_watch_dir = dir;
			open(dir, ec);
			throw_error(ec);
		}

		template <typename Handler>
		BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(Handler,
			void(boost::system::error_code, notify_events))
			async_wait(Handler&& handler)
		{
			return net::async_initiate<Handler,
				void(boost::system::error_code, notify_events)>
				([this](auto&& handler) mutable
				{
					using HandlerType =
						std::decay_t<decltype(handler)>;

					start_op(std::forward<HandlerType>(handler));
				}, handler);
		}


	private:
		template <typename Handler>
		void start_op(Handler&& handler)
		{
			auto slot = net::get_associated_cancellation_slot(handler);
			using unique_array_ptr = std::unique_ptr<uint8_t[]>;

			const DWORD buffer_size = 8192;
			auto bufs = unique_array_ptr(new uint8_t[buffer_size]);
			auto buffer = bufs.get();

			std::memset(buffer, 0, buffer_size);

			auto inside_handler =
				[this,
				bufs = std::move(bufs),
				handler = std::forward<Handler>(handler)]
			(boost::system::error_code ec, size_t bytes_transferred) mutable
			{
				notify_events result;

				if (ec)
				{
					handler(ec, result);
					return;
				}

				this->convert_result(bufs.get(), result);

				handler(ec, result);
			};

			net::windows::overlapped_ptr op(
				this->get_executor(), std::move(inside_handler));

			if (slot.is_connected())
			{
				slot.assign([handle = this->native_handle(),
					op = op.get()](auto type) mutable
				{
					if (boost::asio::cancellation_type::none != type)
						::CancelIoEx(handle, op);
				});
			}

			DWORD transferred = 0;

			BOOL ok = ReadDirectoryChangesW(
				this->native_handle(),
				buffer,
				buffer_size,
				true,
				0x1FF,
				&transferred,
				op.get(),
				nullptr);

			DWORD last_error = GetLastError();
			if (!ok &&
				last_error != ERROR_IO_PENDING &&
				last_error != ERROR_MORE_DATA)
			{
				boost::system::error_code ec{
					static_cast<int>(last_error),
					boost::system::system_category()
				};

				op.complete(ec, (size_t)(transferred));
			}
			else
			{
				op.release();
			}
		}

		inline event_type notify_type(DWORD action) const
		{
			switch (action)
			{
			case FILE_ACTION_ADDED:
				return event_type::creation;
			case FILE_ACTION_REMOVED:
				return event_type::deletion;
			case FILE_ACTION_MODIFIED:
				return event_type::modification;
			case FILE_ACTION_RENAMED_OLD_NAME:
				return event_type::rename;
			case FILE_ACTION_RENAMED_NEW_NAME:
				return event_type::rename;
			default:
				break;
			}
			return event_type::unknown;
		}

		inline void convert_result(uint8_t* data, notify_events& result) const
		{
			auto item = (PFILE_NOTIFY_INFORMATION)data;
			notify_event e;

			for (;;)
			{
				if (item->Action == FILE_ACTION_RENAMED_OLD_NAME)
				{
					e.type_ = notify_type(item->Action);
					e.path_ = m_watch_dir / std::wstring_view{ item->FileName, item->FileNameLength / 2 };

					if (item->NextEntryOffset != 0)
						item = (PFILE_NOTIFY_INFORMATION)((uint8_t*)item + item->NextEntryOffset);
				}
				if (item->Action == FILE_ACTION_RENAMED_NEW_NAME)
				{
					e.new_path_ = m_watch_dir / std::wstring_view{ item->FileName, item->FileNameLength / 2 };
				}
				else
				{
					e.type_ = notify_type(item->Action);
					e.path_ = m_watch_dir / std::wstring_view{ item->FileName, item->FileNameLength / 2 };
				}

				result.emplace_back(e);
				e = {};

				if (item->NextEntryOffset == 0)
					break;

				item = (PFILE_NOTIFY_INFORMATION)((uint8_t*)item + item->NextEntryOffset);
			}
		}

		inline void throw_error(const boost::system::error_code& err,
			boost::source_location const& loc = BOOST_CURRENT_LOCATION)
		{
			if (err)
				boost::throw_exception(boost::system::system_error{ err }, loc);
		}


	private:
		fs::path m_watch_dir;
	};

	using windows_watch = windows_watch_service<>;
}
