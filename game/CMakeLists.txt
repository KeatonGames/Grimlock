add_executable(Game src/main.cpp src/game.cpp)
target_link_libraries(Game PRIVATE Engine)
if(GRIMLOCK_BUILD_EDITOR)
    target_link_libraries(Game PRIVATE Editor)
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
else()
    target_compile_options(Game PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    add_custom_command(TARGET Game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:Game>
        VERBATIM)
endif()
