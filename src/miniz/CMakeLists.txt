# miniz 3.1.2 (https://github.com/richgel999/miniz), vendored from the release amalgamation miniz-3.1.2.zip.
# Used for reading and writing .scopearchive session files.
add_library(miniz STATIC miniz.c)

# Don't define zlib names (deflate, crc32...), which would clash with the real zlib
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_include_directories(miniz SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Third party code: don't fail the build on its warnings
if(NOT MSVC)
	target_compile_options(miniz PRIVATE -w)
endif()
