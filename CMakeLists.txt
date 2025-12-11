cmake_minimum_required(VERSION 3.20)

project(lockfree_list)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lockfree_list
    main.cpp
    lockfree_list.hpp)

target_compile_options(lockfree_list PRIVATE
    -mcx16
    -Wall
    -Wextra
    -Wpedantic
)

option(ENABLE_SANITIZERS "Enable address and thread sanitizers" OFF)

if(ENABLE_SANITIZERS)
    target_compile_options(lockfree_list PRIVATE
        -fsanitize=thread
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(lockfree_list PRIVATE
        -fsanitize=thread
        -fsanitize=undefined
    )
endif()

target_link_libraries(lockfree_list PRIVATE atomic)