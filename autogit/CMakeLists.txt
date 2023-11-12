cmake_minimum_required(VERSION 3.16)
project(autogit)

include(CheckCXXSymbolExists)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DENABLE_LOGGER")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DENABLE_LOGGER -DDISABLE_LOGGER_TO_CONSOLE -DLOGGER_DBG_VIEW")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DENABLE_LOGGER -DDISABLE_LOGGER_TO_CONSOLE -DLOGGER_DBG_VIEW")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -DENABLE_LOGGER -DDISABLE_LOGGER_TO_CONSOLE -DLOGGER_DBG_VIEW")

file(GLOB watchman_HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/include/watchman/notify_event.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/watchman/watchman.hpp
)

file(GLOB watchman_windows_HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/include/watchman/windows/windows_watchman.hpp
)

file(GLOB watchman_linux_HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/include/watchman/linux/linux_watchman.hpp
)

file(GLOB autogit_HEADERS
	${CMAKE_CURRENT_SOURCE_DIR}/include/autogit/logging.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/autogit/scoped_exit.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/autogit/strutil.hpp
)

file(GLOB autogit_SRCS
	${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

add_executable(autogit
	${autogit_HEADERS}
	${watchman_HEADERS}
	${watchman_windows_HEADERS}
	${watchman_linux_HEADERS}
	${autogit_SRCS}
	)

target_link_libraries(autogit
	Boost::asio
	gitpp
)

if (APPLE)
	find_library(CORESERVICES_LIBRARY CoreServices)
	target_link_libraries(autogit ${CORESERVICES_LIBRARY})
endif()

if (HAVE_IOCTL_SIOCGIFADDR)
	target_compile_definitions(autogit PUBLIC HAVE_IOCTL_SIOCGIFADDR)
endif()
if (HAVE_GETIFADDRS)
	target_compile_definitions(autogit PUBLIC HAVE_GETIFADDRS)
endif()

source_group("Header Files/watchman" FILES ${watchman_HEADERS})
source_group("Header Files/watchman/windows" FILES ${watchman_windows_HEADERS})
source_group("Header Files/watchman/linux" FILES ${watchman_linux_HEADERS})

if (MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.29.30129 AND CMAKE_VERSION VERSION_GREATER 3.20.3)
	set_property(TARGET autogit PROPERTY CXX_STANDARD 23)
	message(STATUS "autogit set cxx standard: C++23")
else()
	set_property(TARGET autogit PROPERTY CXX_STANDARD 20)
	message(STATUS "autogit set cxx standard: C++20")
endif()

set(BASE_SOURCE_DIR
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(autogit
	PUBLIC

	${BASE_SOURCE_DIR}
	${Boost_INCLUDE_DIRS}
)
