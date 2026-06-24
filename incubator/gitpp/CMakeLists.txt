
# gitpp  –  Modern C++20 wrapper around libgit2
# =========================================================================

file(GLOB gitpp_sources
	include/gitpp/*.hpp
	src/*.cpp
)

add_library(gitpp STATIC
	${gitpp_sources}
)

target_include_directories(gitpp
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_link_libraries(gitpp
	PUBLIC
		libgit2package
	PRIVATE
		httpc
)

# Require C++20  (for std::source_location, std::span, spaceship operator, etc.)
target_compile_features(gitpp PUBLIC cxx_std_20)

set_target_properties(gitpp PROPERTIES
	FOLDER                          "incubator"
	POSITION_INDEPENDENT_CODE       ON
	CXX_VISIBILITY_PRESET           "hidden"
	VISIBILITY_INLINES_HIDDEN       ON
)

# Give the target a nice alias so user-code can do:
#   target_link_libraries(myapp gitpp::gitpp)
add_library(gitpp::gitpp ALIAS gitpp)
