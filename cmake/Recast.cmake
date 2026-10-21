# Build only the two libraries. Bypass the obsolete upstream CMake minimum and
# avoid demos, DetourCrowd, DebugUtils and tile cache without modifying upstream.
FetchContent_Declare(recastnavigation
    URL https://codeload.github.com/recastnavigation/recastnavigation/tar.gz/refs/tags/v1.6.0
    URL_HASH SHA256=d48ca0121962fa0639502c0f56c4e3ae72f98e55d88727225444f500775c0074
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR grimlock-no-upstream-build)
FetchContent_MakeAvailable(recastnavigation)
foreach(component Recast Detour)
    file(GLOB componentSources CONFIGURE_DEPENDS "${recastnavigation_SOURCE_DIR}/${component}/Source/*.cpp")
    add_library(${component} STATIC ${componentSources})
    add_library(RecastNavigation::${component} ALIAS ${component})
    target_include_directories(${component} SYSTEM PUBLIC "${recastnavigation_SOURCE_DIR}/${component}/Include")
    set_target_properties(${component} PROPERTIES CXX_EXTENSIONS OFF)
endforeach()
