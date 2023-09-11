/* Traits for Outcome
(C) 2018-2023 Niall Douglas <http://www.nedproductions.biz/> (6 commits)
File Created: March 2018


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifndef BOOST_OUTCOME_TRAIT_STD_ERROR_CODE_HPP
#define BOOST_OUTCOME_TRAIT_STD_ERROR_CODE_HPP

#include "../config.hpp"

#include <system_error>

BOOST_OUTCOME_V2_NAMESPACE_BEGIN

namespace detail
{
  // Customise _set_error_is_errno
  template <class State> constexpr inline void _set_error_is_errno(State &state, const std::error_code &error)
  {
    if(error.category() == std::generic_category()
#ifndef _WIN32
       || error.category() == std::system_category()
#endif
    )
    {
      state._status.set_have_error_is_errno(true);
    }
  }
  template <class State> constexpr inline void _set_error_is_errno(State &state, const std::error_condition &error)
  {
    if(error.category() == std::generic_category()
#ifndef _WIN32
       || error.category() == std::system_category()
#endif
    )
    {
      state._status.set_have_error_is_errno(true);
    }
  }
  template <class State> constexpr inline void _set_error_is_errno(State &state, const std::errc & /*unused*/) {
      state._status.set_have_error_is_errno(true);
   }

}  // namespace detail

namespace policy
{
  namespace detail
  {
    /* Pass through `make_error_code` function for `std::error_code`.
     */
    inline std::error_code make_error_code(std::error_code v) { return v; }

    // Try ADL, if not use fall backs above
    template <class T> constexpr inline decltype(auto) error_code(T &&v) { return make_error_code(std::forward<T>(v)); }

    struct std_enum_overload_tag
    {
    };
  }  // namespace detail

  /*! AWAITING HUGO JSON CONVERSION TOOL 
SIGNATURE NOT RECOGNISED
*/
  template <class T> constexpr inline decltype(auto) error_code(T &&v) { return detail::error_code(std::forward<T>(v)); }

  /*! AWAITING HUGO JSON CONVERSION TOOL 
SIGNATURE NOT RECOGNISED
*/
  // inline void outcome_throw_as_system_error_with_payload(...) = delete;  // To use the error_code_throw_as_system_error policy with a custom Error type, you must define a outcome_throw_as_system_error_with_payload() free function to say how to handle the payload
  inline void outcome_throw_as_system_error_with_payload(const std::error_code &error) { BOOST_OUTCOME_THROW_EXCEPTION(std::system_error(error)); }  // NOLINT
  BOOST_OUTCOME_TEMPLATE(class Error)
  BOOST_OUTCOME_TREQUIRES(BOOST_OUTCOME_TPRED(std::is_error_code_enum<std::decay_t<Error>>::value || std::is_error_condition_enum<std::decay_t<Error>>::value))
  inline void outcome_throw_as_system_error_with_payload(Error &&error, detail::std_enum_overload_tag /*unused*/ = detail::std_enum_overload_tag()) { BOOST_OUTCOME_THROW_EXCEPTION(std::system_error(make_error_code(error))); }  // NOLINT
}  // namespace policy

namespace trait
{
  namespace detail
  {
    template <> struct _is_error_code_available<std::error_code>
    {
      // Shortcut this for lower build impact
      static constexpr bool value = true;
      using type = std::error_code;
    };
  }  // namespace detail

  // std::error_code is an error type
  template <> struct is_error_type<std::error_code>
  {
    static constexpr bool value = true;
  };
  // For std::error_code, std::is_error_condition_enum<> is the trait we want.
  template <class Enum> struct is_error_type_enum<std::error_code, Enum>
  {
    static constexpr bool value = std::is_error_condition_enum<Enum>::value;
  };

}  // namespace trait

BOOST_OUTCOME_V2_NAMESPACE_END

#endif
