cmake_minimum_required(VERSION 3.10)
project(GenericLinuxDeviceFramework CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

find_package(Threads REQUIRED)

include_directories(include kernel/virtual_device)

file(GLOB_RECURSE LIB_SOURCES src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/src/cli/main\\.cpp$")
add_library(framework STATIC ${LIB_SOURCES})
target_link_libraries(framework Threads::Threads)

add_executable(device_monitor src/cli/main.cpp)
target_link_libraries(device_monitor framework)

add_executable(temperature_reader examples/temperature_reader/main.cpp)
target_link_libraries(temperature_reader framework)

add_executable(event_subscriber examples/event_subscriber/main.cpp)
target_link_libraries(event_subscriber framework)

add_executable(char_device_client examples/char_device_client/main.cpp)
target_link_libraries(char_device_client framework)

add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark framework)

enable_testing()
file(GLOB TEST_SOURCES
    tests/unit/*.cpp
    tests/integration/*.cpp
    tests/concurrency/*.cpp
    tests/failure_injection/*.cpp)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_include_directories(${test_name} PRIVATE tests)
    target_link_libraries(${test_name} framework)
    add_test(NAME ${test_name} COMMAND ${test_name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

add_executable(driver_test tests/driver/driver_test.cpp)
target_include_directories(driver_test PRIVATE tests)
target_link_libraries(driver_test framework)
