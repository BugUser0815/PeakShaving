cmake_minimum_required(VERSION 3.10)
project(peakshaving CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libspeedwire)

add_executable(peakshaving src/main.cpp)
add_dependencies(peakshaving speedwire)
target_link_libraries(peakshaving speedwire)
target_include_directories(peakshaving PRIVATE speedwire)

include(CTest)
if(BUILD_TESTING)
    add_executable(phase_control_test tests/phase_control_test.cpp)
    add_test(NAME phase_control COMMAND phase_control_test)
    add_executable(phase_obis_test tests/phase_obis_test.cpp)
    target_link_libraries(phase_obis_test speedwire)
    add_test(NAME phase_obis COMMAND phase_obis_test)
endif()
