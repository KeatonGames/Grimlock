include(FetchContent)
# Official stable release, with the matching maintained docking tag.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0-docking.tar.gz
    URL_HASH SHA256=746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
add_library(DearImGui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(DearImGui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imgui_SOURCE_DIR}/misc/cpp")
target_link_libraries(DearImGui PRIVATE SDL3::SDL3 Vulkan::Vulkan)
target_compile_features(DearImGui PUBLIC cxx_std_20)
