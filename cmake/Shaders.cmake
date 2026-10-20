# Slang is supplied by the installed Vulkan SDK; no compiler is downloaded.
find_program(SLANGC_EXECUTABLE NAMES slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
          "${Vulkan_INCLUDE_DIR}/../Bin" "${Vulkan_INCLUDE_DIR}/../bin"
    REQUIRED
)

set(mesh_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang")
set(mesh_spirv "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders/mesh.spv")

add_custom_command(
    OUTPUT "${mesh_spirv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders"
    COMMAND "${SLANGC_EXECUTABLE}" "${mesh_shader}"
        -target spirv -profile spirv_1_5 -emit-spirv-directly
        -fvk-use-entrypoint-name -entry vertMain -entry fragMain
        -matrix-layout-column-major -o "${mesh_spirv}"
    DEPENDS "${mesh_shader}" "${SLANGC_EXECUTABLE}"
    COMMENT "Compiling 3D mesh Slang shaders to SPIR-V"
    VERBATIM
)

set(grid_spirv "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders/grid.spv")
add_custom_command(
    OUTPUT "${grid_spirv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders"
    COMMAND "${SLANGC_EXECUTABLE}" "${mesh_shader}" -DGRID_INSTANCES=1
        -target spirv -profile spirv_1_5 -emit-spirv-directly
        -fvk-use-entrypoint-name -entry vertMain -entry fragMain
        -matrix-layout-column-major -o "${grid_spirv}"
    DEPENDS "${mesh_shader}" "${SLANGC_EXECUTABLE}"
    COMMENT "Compiling instanced grid mesh shaders"
    VERBATIM
)

set(tone_map_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tone_map.slang")
set(tone_map_spirv "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders/tone_map.spv")
add_custom_command(
    OUTPUT "${tone_map_spirv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders"
    COMMAND "${SLANGC_EXECUTABLE}" "${tone_map_shader}"
        -target spirv -profile spirv_1_5 -emit-spirv-directly
        -fvk-use-entrypoint-name -entry vertMain -entry fragMain
        -matrix-layout-column-major -o "${tone_map_spirv}"
    DEPENDS "${tone_map_shader}" "${SLANGC_EXECUTABLE}"
    COMMENT "Compiling HDR tone-mapping shaders"
    VERBATIM
)
add_custom_target(GrimlockShaders DEPENDS "${mesh_spirv}" "${grid_spirv}" "${tone_map_spirv}")
