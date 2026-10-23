cmake_minimum_required(VERSION 3.20)
project(dungeon_queue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(dungeon_simulation src/simulation.cpp)
target_include_directories(dungeon_simulation PUBLIC include)
target_link_libraries(dungeon_simulation PUBLIC Threads::Threads)

add_executable(dungeon_queue src/main.cpp)
target_link_libraries(dungeon_queue PRIVATE dungeon_simulation)

function(enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
enable_warnings(dungeon_simulation)
enable_warnings(dungeon_queue)

include(CTest)
if(BUILD_TESTING)
    add_executable(dungeon_tests tests/tests.cpp)
    target_link_libraries(dungeon_tests PRIVATE dungeon_simulation)
    enable_warnings(dungeon_tests)
    add_test(NAME dungeon_tests COMMAND dungeon_tests)
    set_tests_properties(dungeon_tests PROPERTIES TIMEOUT 30)
    add_test(NAME dungeon_cli
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:dungeon_queue>
            -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/cli-tests
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake)
    set_tests_properties(dungeon_cli PROPERTIES TIMEOUT 20)
endif()

