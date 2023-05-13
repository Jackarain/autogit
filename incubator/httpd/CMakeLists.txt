
file(GLOB httpd_sources
	LIST_DIRECTORIES false
	RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
	CONFIGURE_DEPENDS

	include/httpd/*.hpp
	src/*.cpp
)
add_library(httpd STATIC ${httpd_sources})

target_include_directories(httpd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(httpd PROPERTIES FOLDER "libs")
