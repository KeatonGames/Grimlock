add_library(GameLogic STATIC
    src/entities/enemy_definition.cpp
    src/entities/level_definition.cpp
    src/entities/tower_definition.cpp
    src/systems/placement_system.cpp
    src/systems/wave_system.cpp
    src/systems/level_session.cpp
    src/systems/economy_system.cpp
    src/runtime/level_runtime.cpp
    src/navigation/navigation_geometry.cpp
    src/navigation/nav_mesh.cpp
    src/systems/crowd_movement.cpp
    src/systems/enemy_ai.cpp
    src/systems/tower_system.cpp
    src/systems/enemy_system.cpp
    src/navigation/navigation_session.cpp)
target_include_directories(GameLogic PUBLIC src)
target_link_libraries(GameLogic PUBLIC Engine PRIVATE RecastNavigation::Recast RecastNavigation::Detour nlohmann_json::nlohmann_json)
set_target_properties(GameLogic PROPERTIES CXX_EXTENSIONS OFF)

add_library(GameUi STATIC src/ui/game_hud.cpp)
target_link_libraries(GameUi PUBLIC GameLogic ImGuiSupport PRIVATE SDL3::SDL3 Vulkan::Vulkan)
set_target_properties(GameUi PROPERTIES CXX_EXTENSIONS OFF)
add_executable(Game src/main.cpp src/game.cpp)
target_link_libraries(Game PRIVATE GameUi)
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
    target_compile_options(GameUi PRIVATE /W4 /permissive-)
else()
    target_compile_options(Game PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(GameLogic PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(GameUi PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    add_custom_command(TARGET Game POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:Game> VERBATIM)
endif()
