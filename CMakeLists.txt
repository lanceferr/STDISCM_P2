cmake_minimum_required(VERSION 3.16)
project(dungeon_queue LANGUAGES CXX)

find_package(Threads REQUIRED)
add_executable(dungeon_queue main.cpp)
target_compile_features(dungeon_queue PRIVATE cxx_std_20)
set_target_properties(dungeon_queue PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(dungeon_queue PRIVATE Threads::Threads)
