//
// scoped_exit.hpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2023 Jack (jack.arain at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

#pragma once

#include <type_traits>

#ifdef __cpp_lib_concepts
#	include <concepts>
#endif


template <typename T
#if !defined(__cpp_lib_concepts)
	, typename = typename std::enable_if<std::is_invocable_v<T>>::type
#endif
>
#ifdef __cpp_lib_concepts
	requires std::invocable<T>
#endif
class scoped_exit
{
	scoped_exit() = delete;
	scoped_exit(scoped_exit const&) = delete;
	scoped_exit& operator=(scoped_exit const&) = delete;
	scoped_exit(scoped_exit&&) = delete;
	scoped_exit& operator=(scoped_exit&&) = delete;

public:
	explicit scoped_exit(T&& f)
		: f_(std::move(f))
	{}

	explicit scoped_exit(const T& f)
		: f_(f)
	{}

	~scoped_exit()
	{
		if (stop_token_)
			return;

		f_();
	}

	inline void cancel()
	{
		stop_token_ = true;
	}

private:
	T f_;
	bool stop_token_{ false };
};
