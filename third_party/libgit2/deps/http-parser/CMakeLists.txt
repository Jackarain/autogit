file(GLOB SRC_HTTP "*.c" "*.h")
list(SORT SRC_HTTP)

add_library(http-parser OBJECT ${SRC_HTTP})

enable_warnings(implicit-fallthrough=1)

set_target_properties(http-parser PROPERTIES FOLDER "third_party/libgit2")
