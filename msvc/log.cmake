# Static build of lib/log (logtools) for MSVC, used instead of lib/log/CMakeLists.txt.
# Keep the source list in sync with that file.
set(LOG_DIR "${PROJECT_SOURCE_DIR}/lib/log")

add_library(log STATIC
	${LOG_DIR}/log.cpp
	${LOG_DIR}/ColoredSTDLogSink.cpp
	${LOG_DIR}/STDLogSink.cpp
	${LOG_DIR}/FILELogSink.cpp)

target_include_directories(log
	PUBLIC ${LOG_DIR})
