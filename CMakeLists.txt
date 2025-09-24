cmake_minimum_required(VERSION 4.1)

project(lockfree_list)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lockfree_list
    main.cpp
    lockfree_list.hpp)

target_compile_options(lockfree_list PRIVATE -mcx16)

target_link_libraries(lockfree_list PRIVATE atomic)