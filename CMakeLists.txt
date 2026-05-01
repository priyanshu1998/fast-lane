cmake_minimum_required(VERSION 3.16)

project(fast_lane_shared_memory
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FAST_LANE_BUILD_IMGUI "Build Dear ImGui leader/follower clients" OFF)

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

if(FAST_LANE_BUILD_IMGUI)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui" CACHE PATH "Path to a Dear ImGui checkout")

    if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
        message(FATAL_ERROR "ImGui not found at ${IMGUI_DIR}. Run: git submodule update --init --recursive")
    endif()

    find_package(OpenGL REQUIRED)

    # Build GLFW from the submodule if available, otherwise fall back to system
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/glfw)
    else()
        find_package(glfw3 CONFIG QUIET)
        if(NOT TARGET glfw)
            find_package(glfw3 QUIET)
        endif()
        if(NOT TARGET glfw)
            message(FATAL_ERROR "GLFW not found. Run: git submodule update --init --recursive")
        endif()
    endif()

    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(imgui
        PUBLIC
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
    )

    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    add_executable(imgui_leader src/imgui_leader.cpp)
    target_link_libraries(imgui_leader PRIVATE fast_lane_shm imgui)

    add_executable(imgui_follower src/imgui_follower.cpp)
    target_link_libraries(imgui_follower PRIVATE fast_lane_shm imgui)
endif()

if(MSVC)
    target_compile_options(leader PRIVATE /W4 /permissive-)
    target_compile_options(follower PRIVATE /W4 /permissive-)
    target_compile_options(shared_memory_tests PRIVATE /W4 /permissive-)
    if(FAST_LANE_BUILD_IMGUI)
        target_compile_options(imgui_leader PRIVATE /W4 /permissive-)
        target_compile_options(imgui_follower PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(leader PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(follower PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(shared_memory_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(FAST_LANE_BUILD_IMGUI)
        target_compile_options(imgui_leader PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(imgui_follower PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
