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
