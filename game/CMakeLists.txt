add_library(GameNavigation STATIC
    src/navigation/flow_field.cpp
    src/navigation/agent.cpp
    src/navigation/navigation_demo.cpp)
target_include_directories(GameNavigation PUBLIC src)
target_link_libraries(GameNavigation PUBLIC Engine)
set_target_properties(GameNavigation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(Game src/main.cpp src/game.cpp)
target_link_libraries(Game PRIVATE Engine GameNavigation)
if(GRIMLOCK_BUILD_EDITOR)
    add_library(GameNavigationDebug STATIC src/navigation/navigation_debug.cpp)
    target_link_libraries(GameNavigationDebug PUBLIC GameNavigation Editor PRIVATE DearImGui)
    set_target_properties(GameNavigationDebug PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(Game PRIVATE Editor)
    target_link_libraries(Game PRIVATE GameNavigationDebug)
    target_compile_definitions(Game PRIVATE GRIMLOCK_WITH_EDITOR=1)
endif()
# Preserve the established executable/VS Code paths while exposing the Game target.
set_target_properties(Game PROPERTIES
    OUTPUT_NAME Grimlock
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
    CXX_EXTENSIONS OFF)
add_custom_target(GrimlockAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${PROJECT_BINARY_DIR}/$<CONFIG>/assets"
    COMMENT "Staging game assets"
    VERBATIM)
add_dependencies(Game GrimlockAssets GrimlockShaders)
if(MSVC)
    target_compile_options(Game PRIVATE /W4 /permissive-)
    target_compile_options(GameNavigation PRIVATE /W4 /permissive-)
    if(TARGET GameNavigationDebug)
        target_compile_options(GameNavigationDebug PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(Game PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(GameNavigation PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET GameNavigationDebug)
        target_compile_options(GameNavigationDebug PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET Game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:Game>
        VERBATIM)
endif()
