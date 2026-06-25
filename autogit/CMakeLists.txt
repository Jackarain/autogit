
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
	${autogit_SRCS}
	)

target_link_libraries(autogit
	Boost::asio
	Boost::beast
	Boost::bimap
	gitpp
	httpc
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
