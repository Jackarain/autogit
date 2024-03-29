/*
*
* Copyright (c) 2021
* John Maddock
*
* Use, modification and distribution are subject to the
* Boost Software License, Version 1.0. (See accompanying file
* LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*
*/

#if defined(_WIN32) && !defined(BOOST_REGEX_NO_W32)
//
// Make sure our forward declarations match those in windows.h:
//
#define STRICT
#define BOOST_NO_ANSI_APIS
#include <boost/regex.hpp>
#include <windows.h>

void test_proc()
{
   std::string text, re;
   boost::regex exp(re);
   regex_match(text, exp);
}

#endif
