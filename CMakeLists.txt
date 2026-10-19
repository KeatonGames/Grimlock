cmake_minimum_required(VERSION 3.24)
project(Grimlock VERSION 0.1.0 LANGUAGES C CXX)

find_package(Vulkan 1.3 REQUIRED)
include(cmake/Dependencies.cmake)

add_subdirectory(engine)
option(GRIMLOCK_BUILD_EDITOR "Build the optional Dear ImGui scene editor" ON)
if(GRIMLOCK_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
add_subdirectory(game)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Game)

option(GRIMLOCK_BUILD_TESTS "Build focused Engine checks (some require a Vulkan display and model assets)" OFF)
if(GRIMLOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
