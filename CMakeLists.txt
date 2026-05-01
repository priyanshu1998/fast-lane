cmake_minimum_required(VERSION 3.16)

project(fast_lane_shared_memory
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(fast_lane_shm STATIC
    src/shared_memory.cpp
)

target_include_directories(fast_lane_shm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(fast_lane_shm PRIVATE /W4 /permissive-)
else()
    target_compile_options(fast_lane_shm PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(fast_lane_shm PUBLIC rt)
endif()

add_executable(leader src/leader.cpp)
target_link_libraries(leader PRIVATE fast_lane_shm)

add_executable(follower src/follower.cpp)
target_link_libraries(follower PRIVATE fast_lane_shm)

enable_testing()

add_executable(shared_memory_tests tests/shared_memory_tests.cpp)
target_link_libraries(shared_memory_tests PRIVATE fast_lane_shm)

add_test(NAME shared_memory_tests COMMAND shared_memory_tests)

if(MSVC)
    target_compile_options(leader PRIVATE /W4 /permissive-)
    target_compile_options(follower PRIVATE /W4 /permissive-)
    target_compile_options(shared_memory_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(leader PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(follower PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(shared_memory_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
