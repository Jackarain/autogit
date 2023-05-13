
#pragma once

#include <boost/core/ignore_unused.hpp>
#include <boost/asio.hpp>
#include <boost/asio/windows/overlapped_handle.hpp>

namespace dirmon::detail {

	template<typename IoExecutor = boost::asio::any_io_executor>
	class basic_win_dirchange_read_handle : public boost::asio::windows::basic_overlapped_handle<IoExecutor>
	{
	public:
		template<typename ExecutionContext>
		basic_win_dirchange_read_handle(ExecutionContext&& context, std::string dirname)
			: boost::asio::windows::basic_overlapped_handle<IoExecutor>(std::forward<ExecutionContext>(context), CreateFileW(boost::nowide::widen(dirname).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL))
		{
		}

		~basic_win_dirchange_read_handle(){}

		template<typename MutableBufferSequence, typename Handler>
		auto async_read_some(const MutableBufferSequence& b, Handler&& h)
		{
			return boost::asio::async_initiate<Handler, void(boost::system::error_code, std::size_t)>([this](auto&& handler, auto&& b) mutable
				{
					this->async_read_some_impl(b, std::move(handler));
				}, h, b);
		}

	private:
		template<typename MutableBufferSequence, typename Handler>
		auto async_read_some_impl(const MutableBufferSequence& buffers, Handler&& handler)
		{
			auto slot = boost::asio::get_associated_cancellation_slot(handler);

			boost::asio::windows::overlapped_ptr op(this->get_executor(), std::move(handler));

			// register for cancellation.
			if (slot.is_connected())
			{
				slot.assign([handle_ = this->native_handle(), the_op = op.get()](auto type) mutable
				{
					if (boost::asio::cancellation_type::none != type)
						::CancelIoEx(handle_, the_op);
				});
			}

			auto buffer = boost::asio::detail::buffer_sequence_adapter<
				boost::asio::mutable_buffer, MutableBufferSequence>::first(buffers);

			DWORD bytes_transferred = 0;
			op.get()->Offset = 0;
			op.get()->OffsetHigh = 0;
			BOOL ok = ::ReadDirectoryChangesW(this->native_handle(), buffer.data(),
				static_cast<DWORD>(buffer.size()), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
				, &bytes_transferred, op.get(), NULL);
			DWORD last_error = ::GetLastError();
			if (!ok && last_error != ERROR_IO_PENDING
				&& last_error != ERROR_MORE_DATA)
			{
				boost::system::error_code ec{ static_cast<int>(last_error), boost::system::system_category() };
				op.complete(ec, bytes_transferred);
			}
			else
			{
				op.release();
			}

		}

	};

	using win_dirchange_read_handle = basic_win_dirchange_read_handle<boost::asio::any_io_executor>;
} // namespace dirmon::detail
