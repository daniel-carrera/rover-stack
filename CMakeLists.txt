cmake_minimum_required(VERSION 3.20)
project(guard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # build/compile_commands.json, which editors read

# Every warning is an error.
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

# The sanitizers watch each memory access and each piece of arithmetic while a program runs, and stop it with a
# report the moment something is wrong.
option(GUARD_SANITIZE "address and undefined-behaviour sanitizers" ON)
if(GUARD_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

include_directories(${CMAKE_SOURCE_DIR})   # so that "src/..." and "src/tests/..." are found

# The guard, built once as a library.
add_library(guard src/guard.cpp)

# The programs: one guard file.
# TODO Later, first the core, it waits for the loop, port, and multithreading

# The tests for the guard 
add_executable(test_guard src/tests/test_guard.cpp)

foreach(program test_guard)
  target_link_libraries(${program} PRIVATE guard)
endforeach()

