﻿//
// linux_watchman.hpp
// ~~~~~~~~~~~~~~~~~~
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
#include <boost/asio/posix/stream_descriptor.hpp>

#include <boost/filesystem.hpp>
#include <boost/bimap.hpp>
#include <boost/throw_exception.hpp>

#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <array>
#include <memory>

#include <sys/inotify.h>

#include "watchman/notify_event.hpp"


namespace watchman {

	namespace net = boost::asio;
	namespace fs = boost::filesystem;

	template <typename Executor = net::any_io_executor>
	class linux_watch_service : public net::posix::basic_stream_descriptor<Executor>
	{
	private:
		linux_watch_service(const linux_watch_service&) = delete;
		linux_watch_service& operator=(const linux_watch_service&) = delete;

	public:
		linux_watch_service(const Executor& ex, const fs::path& dir)
			: net::posix::basic_stream_descriptor<Executor>(ex)
			, m_watch_path(dir)
			, m_bufs(std::make_unique<std::array<char, 8192>>())
		{
			open(dir);
		}
		linux_watch_service(const Executor& ex)
			: net::posix::basic_stream_descriptor<Executor>(ex)
			, m_bufs(std::make_unique<std::array<char, 8192>>())
		{}
		~linux_watch_service() = default;

		linux_watch_service(linux_watch_service&&) = default;
		linux_watch_service& operator=(linux_watch_service&&) = default;

		inline void open(const fs::path& dir, boost::system::error_code& ec)
		{
			this->close(ec);

			this->assign(inotify_init1(IN_CLOEXEC | IN_NONBLOCK), ec);
			m_watch_path = dir;

			add_directory(dir);
		}

		inline void open(const fs::path& dir)
		{
			this->close();

			this->assign(inotify_init1(IN_CLOEXEC | IN_NONBLOCK));
			m_watch_path = dir;

			add_directory(dir);
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
			std::memset(m_bufs.get(), 0, 192);

			this->async_read_some(net::buffer(m_bufs.get(), 8192),
				[this, handler = std::move(handler)](
					boost::system::error_code ec,
					std::size_t bytes_transferred) mutable
				{
					notify_events result;

					if (ec)
					{
						handler(ec, result);
						return;
					}

					std::string_view sv((char*)m_bufs.get(), bytes_transferred);
					this->convert_result(sv, result);

					handler(ec, result);
				});
		}

		inline event_type notify_type(uint32_t action, bool& add) const
		{
			switch (action)
			{
			case IN_CREATE:
				return event_type::creation;
			case IN_DELETE:
				return event_type::deletion;
			case IN_MODIFY:
				return event_type::modification;
			case IN_MOVED_FROM:
				return event_type::rename;
			case IN_MOVED_TO:
				return event_type::rename;
			case IN_CREATE | IN_ISDIR:
				add = true;
				return event_type::creation;
			default:
				break;
			}
			return event_type::unknown;
		}

		inline void convert_result(std::string_view sv, notify_events& result)
		{
			notify_event notify;

			m_bufs_pending += std::string(sv);

			while (m_bufs_pending.size() > sizeof(inotify_event))
			{
				const inotify_event* ev =
					(const inotify_event*)(m_bufs_pending.data());

				bool add = false;
				notify.type_ = notify_type(ev->mask, add);
				notify.path_ = ev->name;

				if (add)
				{
					auto fdir = find_dir(ev->wd);
					if (fdir)
					{
						auto dir = *fdir / ev->name;
						add_directory(dir);
					}
				}

				result.push_back(notify);
				m_bufs_pending.erase(0, sizeof(inotify_event) + ev->len);
			}
		}

		inline std::optional<fs::path> find_dir(int wd)
		{
			auto it = m_watch_descriptors.left.find(wd);

			if (it != m_watch_descriptors.left.end())
				return it->second;

			return {};
		}

		inline void add_directory(const fs::path& dir)
		{
			auto wd = inotify_add_watch(
				this->native_handle(),
				dir.string().c_str(),
				IN_CREATE |
				IN_MODIFY |
				IN_MOVED_FROM |
				IN_MOVED_TO |
				IN_DELETE);

			if (wd < 0)
			{
				boost::system::error_code ec(errno,
					boost::system::system_category());

				boost::throw_exception(
					boost::system::system_error{ ec }, BOOST_CURRENT_LOCATION);
			}

			if (wd >= 0)
			{
				m_watch_descriptors.insert(
					watch_descriptors::value_type(wd, dir));

				check_sub_directory(dir, true);
			}
		}

		inline void remove_directory(const fs::path& dir)
		{
			auto it = m_watch_descriptors.right.find(dir);
			if (it != m_watch_descriptors.right.end())
			{
				inotify_rm_watch(this->native_handle(), it->second);
				m_watch_descriptors.right.erase(it);

				check_sub_directory(dir, false);
			}
		}

		inline void check_sub_directory(const fs::path& dir, bool add)
		{
			fs::directory_iterator end;
			for (fs::directory_iterator it(dir); it != end; ++it)
			{
				if (fs::is_directory(*it))
				{
					if (add)
					{
						try
						{
							add_directory(*it);
						}
						catch (const std::exception&)
						{
							continue;
						}
					}
					else
					{
						remove_directory(*it);
					}
				}
			}
		}

	private:
		fs::path m_watch_path;
		using watch_descriptors = boost::bimap<int, fs::path>;
		watch_descriptors m_watch_descriptors;
		std::unique_ptr<std::array<char, 8192>> m_bufs;
		std::string m_bufs_pending;
	};

	using linux_watch = linux_watch_service<>;
}