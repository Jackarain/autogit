//
// macos_watchman.hpp
// ~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2023 Jack (jack.arain at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include <iterator>
#include <cstring>
#include <utility>

#include <boost/asio/post.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>

#include <boost/filesystem.hpp>
#include <boost/throw_exception.hpp>
#include <boost/system.hpp>

#include <CoreServices/CoreServices.h>


#include "watchman/notify_event.hpp"

namespace watchman {
    namespace net = boost::asio;
    namespace fs = boost::filesystem;

    template <typename Executor = net::any_io_executor>
    class macos_watch_service
    {
    private:
        macos_watch_service(const macos_watch_service&) = delete;
        macos_watch_service& operator=(const macos_watch_service&) = delete;

    public:
        macos_watch_service(const Executor& ex, const fs::path& dir,
            const std::vector<fs::path>& excluded_dirs = {})
            : m_executor(ex)
            , m_excluded_dirs(excluded_dirs)
        {
            open(dir);
        }

        explicit macos_watch_service(const Executor& ex,
            const std::vector<fs::path>& excluded_dirs = {})
            : m_executor(ex)
            , m_excluded_dirs(excluded_dirs)
        {}

        ~macos_watch_service()
        {
            boost::system::error_code ignore_ec;
            close(ignore_ec);
        }

        // 自定义移动语义：关闭源对象的流后转移所有权，避免原始指针双重释放。
        macos_watch_service(macos_watch_service&& other) noexcept
            : m_executor(std::move(other.m_executor))
            , m_watch_dir(std::move(other.m_watch_dir))
            , m_stream(other.m_stream)
            , m_fsevents_queue(other.m_fsevents_queue)
            , m_events(std::move(other.m_events))
        {
            other.m_stream = nullptr;
            other.m_fsevents_queue = nullptr;
        }

        macos_watch_service& operator=(macos_watch_service&& other) noexcept
        {
            if (this != &other)
            {
                boost::system::error_code ignore_ec;
                close(ignore_ec);

                m_executor = std::move(other.m_executor);
                m_watch_dir = std::move(other.m_watch_dir);
                m_stream = other.m_stream;
                m_fsevents_queue = other.m_fsevents_queue;
                m_events = std::move(other.m_events);

                other.m_stream = nullptr;
                other.m_fsevents_queue = nullptr;
            }
            return *this;
        }

    public:
        inline void open(const fs::path& dir, boost::system::error_code& ec)
        {
            m_watch_dir = dir;

            CFStringRef dir_ref = CFStringCreateWithCString(
                nullptr, dir.c_str(), kCFStringEncodingUTF8);

            if (!dir_ref)
            {
                ec.assign(errno, boost::system::generic_category());
                return;
            }

            CFArrayRef paths =
                CFArrayCreate(nullptr,
                    reinterpret_cast<const void**>(&dir_ref),
                    1,
                    &kCFTypeArrayCallBacks);

            CFRelease(dir_ref);

            if (!paths)
            {
                ec.assign(errno, boost::system::generic_category());
                return;
            }

            auto context = &m_stream_ctx;

            context->version = 0;
            context->info = this;
            context->retain = nullptr;
            context->release = nullptr;
            context->copyDescription = nullptr;

            FSEventStreamCreateFlags streamFlags = kFSEventStreamCreateFlagFileEvents;
            streamFlags |= kFSEventStreamCreateFlagNoDefer;
            streamFlags |= kFSEventStreamCreateFlagUseExtendedData;
            streamFlags |= kFSEventStreamCreateFlagUseCFTypes;

            m_stream = FSEventStreamCreate(nullptr,
                                 &macos_watch_service<Executor>::fsevents_callback,
                                 context,
                                 paths,
                                 kFSEventStreamEventIdSinceNow,
                                 1,
                                 streamFlags);

            CFRelease(paths);

            if (!m_stream)
            {
                ec.assign(errno, boost::system::generic_category());
                return;
            }

            m_fsevents_queue = dispatch_queue_create("fswatch_event_queue", nullptr);
            FSEventStreamSetDispatchQueue(m_stream, m_fsevents_queue);
            FSEventStreamStart(m_stream);
        }

        inline void open(const fs::path& dir)
        {
            boost::system::error_code ec;
            open(dir, ec);
            throw_error(ec);
        }

        void close(boost::system::error_code& ec)
        {
            if (m_stream == nullptr)
                return;

            FSEventStreamStop(m_stream);
            FSEventStreamInvalidate(m_stream);
            FSEventStreamRelease(m_stream);
            m_stream = nullptr;

            if (m_fsevents_queue)
            {
                dispatch_release(m_fsevents_queue);
                m_fsevents_queue = nullptr;
            }
        }

        void close()
        {
            boost::system::error_code ec;
            close(ec);
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
        bool is_excluded(const fs::path& path) const
        {
            if (m_excluded_dirs.empty())
                return false;

            for (const auto& excluded : m_excluded_dirs)
            {
                // 检查 path 是否在 excluded 目录下或其本身就是被排除的目录。
                auto it = std::mismatch(
                    excluded.begin(), excluded.end(),
                    path.begin(), path.end());
                if (it.first == excluded.end())
                    return true;
            }
            return false;
        }

        template <typename Handler>
        void start_op(Handler&& handler)
        {
            notify_events es;

            // 安全获取锁并交换事件队列，避免 try_lock 丢失事件。
            {
                std::lock_guard<std::mutex> lock(m_event_mtx);
                es.swap(m_events);
                // m_events 已为空，es 持有原事件列表。
            }

            // 使用 post 避免直接调用 handler 造成递归调用而爆栈。
            net::post(m_executor,
                [handler = std::move(handler), es = std::move(es)]() mutable
                {
                    boost::system::error_code ec;
                    handler(ec, es);
                });
        }

        static void fsevents_callback(ConstFSEventStreamRef streamRef,
                                  void* clientCallBackInfo,
                                  size_t numEvents,
                                  void* eventPaths,
                                  const FSEventStreamEventFlags eventFlags[],
                                  const FSEventStreamEventId eventIds[])
        {
            using self_type = macos_watch_service<Executor>;
            auto* fse_monitor = static_cast<self_type*>(clientCallBackInfo);

            CFArrayRef event_array = static_cast<CFArrayRef>(eventPaths);
            std::vector<notify_event> batch;
            batch.reserve(numEvents);

            for (size_t i = 0; i < numEvents; ++i)
            {
                auto path_info_dict = static_cast<CFDictionaryRef>(
                    CFArrayGetValueAtIndex(event_array, i));

                auto path_cfstr = static_cast<CFStringRef>(
                    CFDictionaryGetValue(path_info_dict,
                        kFSEventStreamEventExtendedDataPathKey));

                if (!path_cfstr)
                    continue;

                // 安全地将 CFString 转换为 std::string。
                // CFStringGetCStringPtr 可能返回 NULL，使用 CFStringGetCString 替代。
                CFIndex length = CFStringGetLength(path_cfstr);
                CFIndex max_size = CFStringGetMaximumSizeForEncoding(
                    length, kCFStringEncodingUTF8) + 1;

                std::string path_str;
                path_str.resize(static_cast<std::string::size_type>(max_size));

                if (!CFStringGetCString(path_cfstr, &path_str[0],
                        max_size, kCFStringEncodingUTF8))
                {
                    continue;
                }

                // 按实际长度调整（不含空终止符）。
                path_str.resize(std::strlen(path_str.c_str()));

                // 跳过被排除目录中的事件。
                if (fse_monitor->is_excluded(path_str))
                    continue;

                notify_event event;
                event.path_ = std::move(path_str);

                if (eventFlags[i] & kFSEventStreamEventFlagItemCreated) {
                    event.type_ = event_type::creation;
                } else if (eventFlags[i] & kFSEventStreamEventFlagItemRemoved) {
                    event.type_ = event_type::deletion;
                } else if (eventFlags[i] & kFSEventStreamEventFlagItemRenamed) {
                    event.type_ = event_type::rename;
                } else if (eventFlags[i] & kFSEventStreamEventFlagItemModified) {
                    event.type_ = event_type::modification;
                } else {
                    event.type_ = event_type::unknown;
                }

                batch.push_back(std::move(event));
            }

            if (batch.empty())
                return;

            std::lock_guard<std::mutex> lock(fse_monitor->m_event_mtx);

            // 限制事件队列大小，防止无限增长。
            constexpr std::size_t max_events = 100000;
            if (fse_monitor->m_events.size() > max_events)
            {
                fse_monitor->m_events.erase(
                    fse_monitor->m_events.begin(),
                    fse_monitor->m_events.begin() +
                        (fse_monitor->m_events.size() - max_events));
            }

            fse_monitor->m_events.insert(
                fse_monitor->m_events.end(),
                std::make_move_iterator(batch.begin()),
                std::make_move_iterator(batch.end()));
        }

        inline void throw_error(const boost::system::error_code& err,
            boost::source_location const& loc = BOOST_CURRENT_LOCATION)
        {
            if (err)
                boost::throw_exception(boost::system::system_error{ err }, loc);
        }

    private:
        Executor m_executor;
        fs::path m_watch_dir;
        std::vector<fs::path> m_excluded_dirs;
        FSEventStreamContext m_stream_ctx{};
        FSEventStreamRef m_stream = nullptr;
        dispatch_queue_t m_fsevents_queue = nullptr;
        std::mutex m_event_mtx;
        notify_events m_events;
    };

    using macos_watch = macos_watch_service<>;
}
