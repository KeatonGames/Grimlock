add_library(Engine STATIC
    src/core/engine.cpp
    src/core/startup_profile.cpp
    src/core/transform.cpp
    src/core/orbit_camera.cpp
    src/scene/scene.cpp
    src/scene/scene_serializer.cpp
    src/platform/window.cpp
    src/platform/input.cpp
    src/platform/runtime_paths.cpp
    src/assets/model_loader.cpp
    src/assets/gltf_loader.cpp
    src/assets/gltf_materials.cpp
    src/assets/mesh_data.cpp
    src/assets/image_loader.cpp
    src/assets/stb_image.cpp
    src/assets/tangents.cpp
    src/assets/environment.cpp
    src/assets/animation.cpp
    src/assets/gltf_animation.cpp
    src/renderer/vulkan_renderer.cpp
    src/renderer/vulkan_context.cpp
    src/renderer/mesh_pipeline.cpp
    src/renderer/buffer.cpp
    src/renderer/gpu_mesh.cpp
    src/renderer/depth_target.cpp
    src/renderer/camera.cpp
    src/renderer/texture.cpp
    src/renderer/gpu_materials.cpp
    src/renderer/lighting.cpp
    src/renderer/tone_map_pipeline.cpp
    src/renderer/deformation.cpp
)
target_compile_features(Engine PUBLIC cxx_std_20)
set_target_properties(Engine PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(Engine PUBLIC include PRIVATE src)
target_include_directories(Engine SYSTEM PRIVATE "${simdjson_SOURCE_DIR}/include")
target_link_libraries(Engine PUBLIC glm::glm PRIVATE
    SDL3::SDL3 Vulkan::Vulkan tinyobjloader fastgltf::fastgltf simdjson stb_image mikktspace nlohmann_json::nlohmann_json)
target_compile_definitions(Engine PRIVATE
    STBI_ONLY_PNG STBI_ONLY_JPEG STBI_ONLY_HDR STBI_NO_STDIO STBI_MAX_DIMENSIONS=16384)
# Baseline Debug decoding was ~6 seconds. Optimize only the third-party decoder;
# all engine code retains the normal Debug checks and optimization settings.
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set_source_files_properties(src/assets/stb_image.cpp PROPERTIES
        VS_SETTINGS "Optimization=MaxSpeed;BasicRuntimeChecks=Default")
elseif(NOT MSVC)
    set_source_files_properties(src/assets/stb_image.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()
if(MSVC)
    target_compile_options(Engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(Engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
