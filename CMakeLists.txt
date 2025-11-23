cmake_minimum_required(VERSION 3.10)

project(Gstreamer_Refresh CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Put all binaries in ./build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0)

include_directories(
    ${GST_INCLUDE_DIRS}
)

link_directories(
    ${GST_LIBRARY_DIRS}
)

# Add examples in src/
add_executable(appsink_example
    src/appsink_example.cpp
)

target_link_libraries(appsink_example
    ${GST_LIBRARIES}
)

# Add appsrc example
add_executable(appsrc_to_mp4
    src/appsrc_to_mp4.cpp
)

# attach include dirs and compile flags to this target
target_include_directories(appsrc_to_mp4
    PRIVATE
        ${GST_INCLUDE_DIRS}
)

target_compile_options(appsrc_to_mp4
    PRIVATE
        ${GST_CFLAGS_OTHER}
)

target_link_libraries(appsrc_to_mp4
    PRIVATE
        ${GST_LIBRARIES}
)