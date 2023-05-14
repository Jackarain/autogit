
file(GLOB dirmon_sources
	include/dirmon/*.hpp
	include/dirmon/windows/*.hpp
	src/*.cpp
)
add_library(dirmon STATIC ${dirmon_sources})
target_link_libraries(dirmon Boost::asio)

target_include_directories(dirmon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(dirmon PROPERTIES FOLDER "libs")

add_executable(dirmon_test test.cpp)

target_link_libraries(dirmon_test dirmon Boost::asio)
