include(FetchContent)

# SDL3 is fetched as a shared library. Its DLL is copied next to Grimlock on Windows.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
foreach(subsystem AUDIO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI POWER SENSOR DIALOG TRAY)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz
    URL_HASH SHA256=c2ee715e42ec520c4d11fd8d249ef1d2b2baf4ad31148b72b3b000276c0b3633
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# GLM is header-only. Do not add its tests or installation targets to this project.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
    URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Stable 1.x release. Its obsolete upstream CMake minimum is incompatible with
# CMake 4, so consume the two documented library sources without its examples/install rules.
FetchContent_Declare(
    tinyobjloader
    URL https://github.com/tinyobjloader/tinyobjloader/archive/refs/tags/v1.0.7.tar.gz
    URL_HASH SHA256=b9d08b675ba54b9cb00ffc99eaba7616d0f7e6f6b8947a7e118474e97d942129
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR grimlock-no-upstream-build
)

# fastgltf's required JSON parser is pinned here rather than downloaded implicitly
# by fastgltf, so system packages cannot change the build's dependency version.
set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
set(SIMDJSON_INSTALL OFF CACHE BOOL "" FORCE)
set(SIMDJSON_CXX_STANDARD 20 CACHE STRING "" FORCE)
FetchContent_Declare(
    simdjson
    URL https://github.com/simdjson/simdjson/archive/refs/tags/v4.6.11.tar.gz
    URL_HASH SHA256=61d948fc24f0d793829ad658058e7597d064988a89b4607ea02e401a82df98ff
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    fastgltf
    URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.9.1.tar.gz
    URL_HASH SHA256=d8705a496deb2831775baeef5009be36d24bf3d10f8d29ab016e977d0119c872
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# stb has no release tags; pin the complete revision and archive digest.
FetchContent_Declare(
    stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR grimlock-no-upstream-build
)

FetchContent_Declare(
    mikktspace
    URL https://github.com/mmikk/MikkTSpace/archive/3e895b49d05ea07e4c2133156cfa94369e19e409.tar.gz
    URL_HASH SHA256=aeba65ddee85a679133d510d71d00b108f603752f90b15b9ecf7777033f6e351
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR grimlock-no-upstream-build
)
# Scene serialization needs a mutable JSON document/writer; simdjson remains the
# fastgltf parser. Keep the new header-only dependency private to Engine.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3 glm tinyobjloader simdjson fastgltf stb mikktspace nlohmann_json)
add_library(mikktspace STATIC "${mikktspace_SOURCE_DIR}/mikktspace.c")
target_include_directories(mikktspace SYSTEM PUBLIC "${mikktspace_SOURCE_DIR}")
add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(tinyobjloader STATIC "${tinyobjloader_SOURCE_DIR}/tiny_obj_loader.cc")
target_include_directories(tinyobjloader SYSTEM PUBLIC "${tinyobjloader_SOURCE_DIR}")
