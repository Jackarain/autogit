
file(GLOB gitpp_sources
	include/gitpp/*.hpp
	src/*.cpp
)
add_library(gitpp STATIC ${gitpp_sources})

target_include_directories(gitpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(gitpp git2)

set_target_properties(gitpp PROPERTIES FOLDER "libs")
