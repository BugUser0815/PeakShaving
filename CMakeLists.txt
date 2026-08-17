cmake_minimum_required(VERSION 3.10)
project(peakshaving CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libspeedwire)

add_executable(peakshaving src/main.cpp)
add_dependencies(peakshaving speedwire)
target_link_libraries(peakshaving speedwire)
target_include_directories(peakshaving PRIVATE speedwire)
