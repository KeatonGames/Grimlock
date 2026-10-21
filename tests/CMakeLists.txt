# A real Engine consumer: only its public headers and link interface are available.
add_executable(GridClearanceChecks grid_clearance.cpp)
target_link_libraries(GridClearanceChecks PRIVATE GameLogic)
add_test(NAME Terrain.Clearance COMMAND GridClearanceChecks)
add_executable(EnemyCrowdChecks enemy_crowd.cpp)
target_link_libraries(EnemyCrowdChecks PRIVATE GameLogic)
add_test(NAME Enemies.Crowd COMMAND EnemyCrowdChecks)

add_executable(EngineApiChecks engine_api.cpp)
target_link_libraries(EngineApiChecks PRIVATE Engine)
set_target_properties(EngineApiChecks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
    CXX_EXTENSIONS OFF)
add_dependencies(EngineApiChecks GrimlockShaders GrimlockAssets)
if(WIN32)
    add_custom_command(TARGET EngineApiChecks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:EngineApiChecks>
        VERBATIM)
endif()
add_test(NAME Engine.PublicApi COMMAND EngineApiChecks "${PROJECT_SOURCE_DIR}/game/assets/models/blackrat.glb")

add_executable(SceneChecks scene.cpp)
target_link_libraries(SceneChecks PRIVATE Engine nlohmann_json::nlohmann_json)
add_test(NAME Scene.Cpu COMMAND SceneChecks "${PROJECT_SOURCE_DIR}/game" "${CMAKE_CURRENT_BINARY_DIR}/scene-output")
add_executable(GridChecks grid.cpp)
target_include_directories(GridChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(GridChecks PRIVATE Engine nlohmann_json::nlohmann_json)
add_test(NAME Grid.Cpu COMMAND GridChecks "${PROJECT_SOURCE_DIR}/game" "${CMAKE_CURRENT_BINARY_DIR}/grid-output")

add_executable(SceneRuntimeChecks scene_runtime.cpp)
target_link_libraries(SceneRuntimeChecks PRIVATE Engine nlohmann_json::nlohmann_json)
set_target_properties(SceneRuntimeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
add_dependencies(SceneRuntimeChecks GrimlockShaders GrimlockAssets)
if(WIN32)
    add_custom_command(TARGET SceneRuntimeChecks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:SceneRuntimeChecks>
        VERBATIM)
endif()
add_test(NAME Scene.Runtime COMMAND SceneRuntimeChecks "${PROJECT_SOURCE_DIR}/game" "${CMAKE_CURRENT_BINARY_DIR}/scene-runtime")
set_tests_properties(Engine.PublicApi Scene.Runtime PROPERTIES RUN_SERIAL TRUE)

add_executable(CameraNavigationChecks camera_navigation.cpp)
target_link_libraries(CameraNavigationChecks PRIVATE Engine)
add_test(NAME Camera.Navigation COMMAND CameraNavigationChecks)

add_executable(AnimationChecks animation.cpp)
target_link_libraries(AnimationChecks PRIVATE Engine)
add_test(NAME Animation.Cpu COMMAND AnimationChecks)

add_executable(InputChecks input.cpp)
target_include_directories(InputChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(InputChecks PRIVATE Engine SDL3::SDL3)
if(WIN32)
    add_custom_command(TARGET InputChecks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:InputChecks>
        VERBATIM)
endif()
add_test(NAME Input.State COMMAND InputChecks)
set_tests_properties(Input.State PROPERTIES RUN_SERIAL TRUE)

add_executable(NavigationRuntimeChecks navigation_runtime.cpp)
target_link_libraries(NavigationRuntimeChecks PRIVATE Engine SDL3::SDL3)
set_target_properties(NavigationRuntimeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
add_dependencies(NavigationRuntimeChecks GrimlockShaders)
if(WIN32)
    add_custom_command(TARGET NavigationRuntimeChecks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:NavigationRuntimeChecks>
        VERBATIM)
endif()
add_test(NAME Camera.Runtime COMMAND NavigationRuntimeChecks "${CMAKE_CURRENT_BINARY_DIR}/navigation-camera.json")
set_tests_properties(Camera.Runtime PROPERTIES RUN_SERIAL TRUE)

add_executable(PickingChecks picking.cpp)
target_include_directories(PickingChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(PickingChecks PRIVATE Engine)
add_test(NAME Picking.Cpu COMMAND PickingChecks "${PROJECT_SOURCE_DIR}/game/assets/models")

add_executable(GltfPrimitiveChecks gltf_primitive.cpp)
target_include_directories(GltfPrimitiveChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(GltfPrimitiveChecks PRIVATE Engine nlohmann_json::nlohmann_json)
add_test(NAME Assets.GltfPrimitive COMMAND GltfPrimitiveChecks "${CMAKE_CURRENT_BINARY_DIR}/gltf-primitive")

add_executable(GltfModelChecks gltf_model.cpp)
target_include_directories(GltfModelChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(GltfModelChecks PRIVATE Engine nlohmann_json::nlohmann_json)
add_test(NAME Assets.GltfModel COMMAND GltfModelChecks "${CMAKE_CURRENT_BINARY_DIR}/gltf-model")

add_executable(GltfAnimationChecks gltf_animation.cpp)
target_include_directories(GltfAnimationChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(GltfAnimationChecks PRIVATE Engine fastgltf::fastgltf)
add_test(NAME Assets.GltfAnimation COMMAND GltfAnimationChecks)

add_executable(GltfMaterialChecks gltf_materials.cpp)
target_include_directories(GltfMaterialChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(GltfMaterialChecks PRIVATE Engine fastgltf::fastgltf)
add_test(NAME Assets.GltfMaterials COMMAND GltfMaterialChecks)

add_executable(RendererRuntimeChecks renderer_runtime.cpp)
target_include_directories(RendererRuntimeChecks PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(RendererRuntimeChecks PRIVATE Engine SDL3::SDL3 Vulkan::Vulkan)
set_target_properties(RendererRuntimeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
add_dependencies(RendererRuntimeChecks GrimlockShaders)
if(WIN32)
    add_custom_command(TARGET RendererRuntimeChecks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:RendererRuntimeChecks>
        VERBATIM)
endif()
add_test(NAME Renderer.Runtime COMMAND RendererRuntimeChecks "${PROJECT_SOURCE_DIR}/game/assets/models/cube.obj")
set_tests_properties(Renderer.Runtime PROPERTIES RUN_SERIAL TRUE)

add_executable(SelectionRuntimeChecks selection_runtime.cpp)
target_link_libraries(SelectionRuntimeChecks PRIVATE Engine SDL3::SDL3)
set_target_properties(SelectionRuntimeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
if(WIN32)
    add_custom_command(TARGET SelectionRuntimeChecks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:SelectionRuntimeChecks>
        VERBATIM)
endif()
add_test(NAME Picking.Runtime COMMAND SelectionRuntimeChecks "${PROJECT_SOURCE_DIR}/game" "${CMAKE_CURRENT_BINARY_DIR}/selection.json")
set_tests_properties(Picking.Runtime PROPERTIES RUN_SERIAL TRUE)

if(GRIMLOCK_BUILD_EDITOR)
    add_executable(EnemyRuntimeChecks enemy_runtime.cpp)
    target_include_directories(EnemyRuntimeChecks PRIVATE "${PROJECT_SOURCE_DIR}/editor/src")
    target_link_libraries(EnemyRuntimeChecks PRIVATE GameDebug DearImGui SDL3::SDL3 Vulkan::Vulkan)
    set_target_properties(EnemyRuntimeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
    if(WIN32)
        add_custom_command(TARGET EnemyRuntimeChecks POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:EnemyRuntimeChecks>
            VERBATIM)
    endif()
    add_test(NAME Enemies.Runtime COMMAND EnemyRuntimeChecks "${PROJECT_SOURCE_DIR}/game" "${CMAKE_CURRENT_BINARY_DIR}/enemy-runtime.json")
    set_tests_properties(Enemies.Runtime PROPERTIES RUN_SERIAL TRUE)

    add_executable(EditorRuntimeChecks editor_runtime.cpp)
    target_include_directories(EditorRuntimeChecks PRIVATE "${PROJECT_SOURCE_DIR}/editor/src")
    target_link_libraries(EditorRuntimeChecks PRIVATE Editor DearImGui SDL3::SDL3 Vulkan::Vulkan)
    set_target_properties(EditorRuntimeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>" CXX_EXTENSIONS OFF)
    if(WIN32)
        add_custom_command(TARGET EditorRuntimeChecks POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:EditorRuntimeChecks>
            VERBATIM)
    endif()
    add_test(NAME Editor.Runtime COMMAND EditorRuntimeChecks "${PROJECT_SOURCE_DIR}/game" "${CMAKE_CURRENT_BINARY_DIR}/editor-output")
    set_tests_properties(Editor.Runtime PROPERTIES RUN_SERIAL TRUE)
endif()
