add_library(GameLogic STATIC
    src/entities/hero.cpp
    src/systems/hero_system.cpp
    src/systems/hero_combat_system.cpp
    src/systems/enemy_combat_system.cpp
    src/systems/melee_navigation.cpp
    src/entities/enemy_definition.cpp
    src/entities/level_definition.cpp
    src/entities/tower_definition.cpp
    src/systems/placement_system.cpp
    src/systems/build_zone_markers.cpp
    src/ui/hero_route_visual.cpp
    src/systems/wave_system.cpp
    src/systems/level_session.cpp
    src/systems/economy_system.cpp
    src/runtime/level_runtime.cpp
    src/runtime/level_catalog.cpp
    src/runtime/player_progress.cpp
    src/navigation/navigation_geometry.cpp
    src/navigation/level_geometry.cpp
    src/navigation/nav_mesh.cpp
    src/systems/crowd_movement.cpp
    src/systems/enemy_ai.cpp
    src/systems/combat_system.cpp
    src/systems/combat_geometry.cpp
    src/systems/projectile_system.cpp
    src/systems/tower_system.cpp
    src/systems/enemy_system.cpp
    src/navigation/navigation_session.cpp)
target_include_directories(GameLogic PUBLIC src)
target_link_libraries(GameLogic PUBLIC Engine PRIVATE RecastNavigation::Recast RecastNavigation::Detour nlohmann_json::nlohmann_json)
set_target_properties(GameLogic PROPERTIES CXX_EXTENSIONS OFF)
option(GRIMLOCK_USE_SOURCE_ASSETS "Use the shared source asset tree in development launchers" ON)
if(GRIMLOCK_USE_SOURCE_ASSETS)
    target_compile_definitions(GameLogic PRIVATE GRIMLOCK_SOURCE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()

add_library(GameUi STATIC src/ui/game_hud.cpp src/ui/game_style.cpp src/ui/game_menu.cpp)
target_link_libraries(GameUi PUBLIC GameLogic ImGuiSupport PRIVATE SDL3::SDL3 Vulkan::Vulkan stb_image nlohmann_json::nlohmann_json)
set_target_properties(GameUi PROPERTIES CXX_EXTENSIONS OFF)
add_executable(Game src/main.cpp src/game.cpp)
target_link_libraries(Game PRIVATE GameUi SDL3::SDL3)
# Preserve the established executable/VS Code paths while exposing the Game target.
set_target_properties(Game PROPERTIES
    OUTPUT_NAME Grimlock
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
    CXX_EXTENSIONS OFF)
add_custom_target(GrimlockAssets
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "-DDESTINATION=${PROJECT_BINARY_DIR}/$<CONFIG>/assets"
        "-DBINARY_ROOT=${PROJECT_BINARY_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/stage_assets.cmake"
    COMMENT "Staging game assets"
    VERBATIM)
add_dependencies(Game GrimlockAssets GrimlockShaders)

# Explicit authoring tool: captures real Vulkan frames; never runs at Game startup.
add_executable(MenuCapture EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/tools/capture_menu.cpp")
target_link_libraries(MenuCapture PRIVATE GameUi stb_image SDL3::SDL3)
set_target_properties(MenuCapture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
add_dependencies(MenuCapture GrimlockShaders)
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
