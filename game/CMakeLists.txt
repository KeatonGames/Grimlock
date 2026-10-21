add_library(GameLogic STATIC
    src/navigation/navigation_geometry.cpp
    src/navigation/nav_mesh.cpp
    src/systems/crowd_movement.cpp
    src/systems/enemy_system.cpp
    src/navigation/navigation_session.cpp)
target_include_directories(GameLogic PUBLIC src)
target_link_libraries(GameLogic PUBLIC Engine PRIVATE RecastNavigation::Recast RecastNavigation::Detour)
set_target_properties(GameLogic PROPERTIES CXX_EXTENSIONS OFF)

add_executable(Game src/main.cpp src/game.cpp)
target_link_libraries(Game PRIVATE Engine GameLogic)
if(GRIMLOCK_BUILD_EDITOR)
    add_library(GameDebug STATIC src/debug/navigation_debug.cpp)
    target_link_libraries(GameDebug PUBLIC GameLogic Editor PRIVATE DearImGui)
    set_target_properties(GameDebug PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(Game PRIVATE Editor)
    target_link_libraries(Game PRIVATE GameDebug)
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
    target_compile_options(GameLogic PRIVATE /W4 /permissive-)
    if(TARGET GameDebug)
        target_compile_options(GameDebug PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(Game PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(GameLogic PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET GameDebug)
        target_compile_options(GameDebug PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET Game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:Game>
        VERBATIM)
endif()
