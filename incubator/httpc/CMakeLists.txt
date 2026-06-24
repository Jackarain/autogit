#
# httpc  –  Modern C++20 http client library
#

file(GLOB httpc_sources
	include/httpc/*.hpp
	src/*.cpp
)

add_library(httpc STATIC
	${httpc_sources}
)

target_include_directories(httpc
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_link_libraries(httpc
	PUBLIC
    Boost::asio
)

target_compile_features(httpc PUBLIC cxx_std_20)

set_target_properties(gitpp PROPERTIES
	FOLDER                          "incubator"
	POSITION_INDEPENDENT_CODE       ON
	CXX_VISIBILITY_PRESET           "hidden"
	VISIBILITY_INLINES_HIDDEN       ON
)
