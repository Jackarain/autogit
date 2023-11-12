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
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>

#include <boost/filesystem.hpp>
#include <boost/throw_exception.hpp>

#include <CoreServices/CoreServices.h>

#include "boost/system/detail/error_code.hpp"
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
        macos_watch_service(const Executor& ex, const fs::path& dir)
            : m_executor(ex)
        {
            open(dir);
        }
        explicit macos_watch_service(const Executor& ex)
            : m_executor(ex)
        {}
        ~macos_watch_service()
        {
            boost::system::error_code ignore_ec;
            close(ignore_ec);
        }

        macos_watch_service(macos_watch_service&&) = default;
        macos_watch_service& operator=(macos_watch_service&&) = default;

    public:
        inline void open(const fs::path& dir, boost::system::error_code& ec)
        {
            m_watch_dir = dir;

            CFStringRef dir_ref = CFStringCreateWithCString(
                nullptr, dir.c_str(), kCFStringEncodingUTF8);

            CFArrayRef paths =
                CFArrayCreate(nullptr,
                    reinterpret_cast<const void **> (&dir_ref),
                    1.f,
                    &kCFTypeArrayCallBacks);

            auto context = &m_stream_ctx;

            context->version = 0;
            context->info = this;
            context->retain = nullptr;
            context->release = nullptr;
            context->copyDescription = nullptr;

            FSEventStreamCreateFlags streamFlags = kFSEventStreamCreateFlagFileEvents;
            streamFlags |= kFSEventStreamCreateFlagNoDefer;

// #if defined (HAVE_MACOS_GE_10_13)
            streamFlags |= kFSEventStreamCreateFlagUseExtendedData;
            streamFlags |= kFSEventStreamCreateFlagUseCFTypes;
// #endif
            m_stream = FSEventStreamCreate(nullptr,
                                 &macos_watch_service<Executor>::fsevents_callback,
                                 context,
                                 paths,
                                 kFSEventStreamEventIdSinceNow,
                                 1,
                                 streamFlags);

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
            dispatch_release(m_fsevents_queue);

            m_stream = nullptr;
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
        template <typename Handler>
        void start_op(Handler&& handler)
        {
            boost::system::error_code ec;
            notify_events es;
            handler(ec, es);
        }

        static void fsevents_callback(ConstFSEventStreamRef streamRef,
                                  void *clientCallBackInfo,
                                  size_t numEvents,
                                  void *eventPaths,
                                  const FSEventStreamEventFlags eventFlags[],
                                  const FSEventStreamEventId eventIds[])
        {
            using self_type = macos_watch_service<Executor>;
            auto* fse_monitor = (self_type*) (clientCallBackInfo);

            for (size_t i = 0; i < numEvents; ++i)
            {
                auto path_info_dict = reinterpret_cast<CFDictionaryRef>(
                    CFArrayGetValueAtIndex((CFArrayRef) eventPaths, i));
                auto path = reinterpret_cast<CFStringRef>(
                    CFDictionaryGetValue(path_info_dict,
                        kFSEventStreamEventExtendedDataPathKey));

                notify_event event;
                event.path_ = std::string(CFStringGetCStringPtr(path, kCFStringEncodingUTF8));
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

                std::cout << "event: " << eventFlags[i] << ", dir: " << event.path_ << "\n";

                fse_monitor->m_events.push_back(event);
            }
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
        FSEventStreamContext m_stream_ctx;
        FSEventStreamRef m_stream = nullptr;
        dispatch_queue_t m_fsevents_queue = nullptr;
        notify_events m_events;
    };

    using macos_watch = macos_watch_service<>;
}
