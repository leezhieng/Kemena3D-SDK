cmake_minimum_required(VERSION 3.16)

project(Kemena3DSDK
    VERSION 0.0.1
    DESCRIPTION "Kemena3D SDK"
    LANGUAGES C CXX
)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to build shared or static library
option(BUILD_SHARED_LIBS "Build as shared library" OFF)

# ---------------------------------------------------------------------------
# OpenGL ES 3.0 backend (Android / iOS / embedded)
#
# The ES backend is the only graphics backend available on these platforms and
# is never used on desktop, where kOpenGLDriver / kDX11Driver is the correct
# choice.  kopenglesdriver.cpp is removed from the source list unless the
# target is genuinely an ES platform, so desktop builds never require the
# GLES3/EGL headers or symbols.
#
# Eligible platforms: Android, iOS, Emscripten (WebGL 2), or any target the
# embedder opts in with -DKEMENA_GLES_EMBEDDED=ON (vendor GLES SDKs such as
# Mali, PowerVR or Vivante).
# ---------------------------------------------------------------------------
option(KEMENA_GLES "Use the OpenGL ES 3.0 backend (mobile / embedded)" OFF)
option(KEMENA_GLES_EMBEDDED "Target is an embedded Linux device using EGL + GLES" OFF)
option(KEMENA_GLES_ALLOW_EMULATION "Permit KEMENA_GLES on desktop via ANGLE/Mesa" OFF)
set(KEMENA_GLES_INCLUDE_DIR "" CACHE PATH "Extra include root for GLES3/EGL headers")

set(KEMENA_GLES_PLATFORM "")
if(ANDROID)
    set(KEMENA_GLES_PLATFORM "Android")
elseif(IOS)
    set(KEMENA_GLES_PLATFORM "iOS")
elseif(EMSCRIPTEN)
    set(KEMENA_GLES_PLATFORM "Emscripten")
elseif(KEMENA_GLES_EMBEDDED)
    set(KEMENA_GLES_PLATFORM "Embedded Linux")
endif()

# Turn the backend on automatically where nothing else is available.
if(NOT KEMENA_GLES AND NOT KEMENA_GLES_PLATFORM STREQUAL "")
    set(KEMENA_GLES ON)
    message(STATUS "Kemena3D SDK: enabling OpenGL ES 3.0 backend for ${KEMENA_GLES_PLATFORM}")
endif()

# Refuse the ES backend on a desktop target unless emulation was requested
# explicitly; silently building it there would link against a GLES loader that
# is not present and produce confusing link errors.
if(KEMENA_GLES AND KEMENA_GLES_PLATFORM STREQUAL "")
    if(NOT KEMENA_GLES_ALLOW_EMULATION)
        message(FATAL_ERROR
            "KEMENA_GLES is a mobile/embedded-only backend and cannot be built for "
            "this desktop target. Use RENDERER_GL or RENDERER_D3D11, or configure "
            "with -DKEMENA_GLES_ALLOW_EMULATION=ON when building against ANGLE/Mesa.")
    endif()
    set(KEMENA_GLES_PLATFORM "Desktop (emulated)")
    message(STATUS "Kemena3D SDK: KEMENA_GLES emulated on desktop via ANGLE/Mesa")
endif()

set(KEMENA_HAS_GLES FALSE)
if(KEMENA_GLES)
    set(KEMENA_HAS_GLES TRUE)
endif()

# When ON  (default): Assimp drives mesh / animation import — full format set,
#                     editor-style build.
# When OFF           : Assimp is not compiled / linked. tinygltf (header-only)
#                     handles glTF and GLB imports; everything else fails to
#                     load. Saving via Assimp::Exporter is also unavailable.
#                     This is the slim "runtime" SDK build.
option(KEMENA_USE_ASSIMP "Include Assimp for full-format mesh import" ON)
if(NOT KEMENA_USE_ASSIMP)
    message(STATUS "Kemena3D SDK: building WITHOUT Assimp (tinygltf-only mesh import)")
endif()

# Optional MinGW flag (set via -G)
if(WIN32)
    option(USE_MINGW "Use MinGW on Windows" OFF)
    if(USE_MINGW)
        message(STATUS "Using MinGW")
        add_definitions(-DUSE_MINGW)
    else()
        message(STATUS "Using MSVC")
        add_definitions(-DUSE_MSVC)
    endif()
endif()

# Collect core source and header files
file(GLOB_RECURSE SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Source/src/*.cpp)
file(GLOB_RECURSE HEADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Source/include/*.h)

# Exactly one GL backend is compiled per platform.  Removing the files here
# (instead of guarding their bodies with #if) keeps each platform's translation
# unit set and header graph clean:
#   * Desktop keeps kOpenGLDriver (GLEW + <GL/gl.h>).
#   * ES platforms keep kOpenGLESDriver (GLES3 + EGL).  The desktop driver
#     cannot compile there because <GL/glew.h> and <GL/gl.h> do not exist.
if(KEMENA_HAS_GLES)
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/src/kopengldriver.cpp)
    list(REMOVE_ITEM HEADER_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/include/kopengldriver.h)
else()
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/src/kopenglesdriver.cpp)
    list(REMOVE_ITEM HEADER_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/include/kopenglesdriver.h)
endif()

# Miniaudio custom decoder backends (libvorbis, libopus)
file(GLOB MINIAUDIO_DECODER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libvorbis/miniaudio_libvorbis.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libopus/miniaudio_libopus.c
)

# Collect AngelScript add_on sources, and remove generateheader.cpp (which defines main)
file(GLOB_RECURSE ANGELSCRIPT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/add_on/*.cpp)
list(REMOVE_ITEM ANGELSCRIPT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/add_on/scriptbuilder/generateheader.cpp")
file(GLOB_RECURSE ANGELSCRIPT_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/add_on/*.h)

# ImGui sources
file(GLOB IMGUI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/*.cpp)
file(GLOB IMGUI_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/*.h)
file(GLOB IMGUI_BACKENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/backends/imgui_impl_sdl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/backends/imgui_impl_opengl3.cpp
)
if(KEMENA_D3D11)
    list(APPEND IMGUI_BACKENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/backends/imgui_impl_dx11.cpp
    )
endif()

# ImGuizmo sources
file(GLOB IMGUIZMO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imguizmo/src/*.cpp)
file(GLOB IMGUIZMO_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imguizmo/src/*.h)

# Create library target
add_library(Kemena3DSDK ${SRC_FILES} ${HEADER_FILES} ${ANGELSCRIPT_SRC} ${ANGELSCRIPT_HEADERS} ${IMGUI_SRC} ${IMGUI_HEADERS} ${IMGUI_BACKENDS} ${IMGUIZMO_SRC} ${IMGUIZMO_HEADERS} ${MINIAUDIO_DECODER_SRC})

set_target_properties(Kemena3DSDK PROPERTIES
    VERSION ${PROJECT_VERSION}
    PUBLIC_HEADER "${HEADER_FILES}"

    # Debug build output
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Output/lib/Debug"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Output/lib/Debug"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Output/lib/Debug"

    # Release build output
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Output/lib/Release"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Output/lib/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Output/lib/Release"
)

# Include directories
target_include_directories(Kemena3DSDK
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/include
		${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui
		${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/add_on
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/sdl/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/assimp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/assimp/build_$<CONFIG>/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/tinygltf
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glm
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/miniaudio
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libvorbis
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libopus
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/ogg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/vorbis/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/opus/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/opusfile/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/nlohmann/include
		${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/portable-file-dialogs
		${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imguizmo/src
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt
        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/include/recastnavigation
)

# Link directories (per-config)
link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/sdl/build_$<CONFIG>
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/build_$<CONFIG>
    ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/assimp/build_$<CONFIG>
)

# Definitions and static linking behavior
if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(Kemena3DSDK PRIVATE GLEW_STATIC)
    set(GLEW_LIB libglew32)
else()
    set(GLEW_LIB glew32)
endif()

# ImGui's OpenGL3 backend uses the GLEW loader on desktop and the built-in
# native ES3 loader on mobile/embedded (where GLEW does not exist).
if(KEMENA_HAS_GLES)
    target_compile_definitions(Kemena3DSDK PRIVATE IMGUI_IMPL_OPENGL_ES3)
else()
    target_compile_definitions(Kemena3DSDK PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLEW)
endif()

# Runtime/editor switch: KEMENA_NO_ASSIMP is the source-side gate every
# import / export path keys off. Defined PUBLIC so consumers (studio, runtime)
# see the same value as the SDK objects.
if(NOT KEMENA_USE_ASSIMP)
    target_compile_definitions(Kemena3DSDK PUBLIC KEMENA_NO_ASSIMP)
endif()

# Jolt was built with default options (DEBUG_RENDERER_IN_DEBUG_AND_RELEASE,
# PROFILER_IN_DEBUG_AND_RELEASE, ENABLE_OBJECT_STREAM, FLOATING_POINT_EXCEPTIONS_ENABLED),
# all PUBLIC on the Jolt target. Since we link the prebuilt Jolt.lib directly (no imported
# target), we must mirror the same defines here. A mismatch makes JPH_VERSION_ID at the
# kPhysicsManager::init call site disagree with Jolt.lib, and RegisterTypes() std::abort()s.
target_compile_definitions(Kemena3DSDK PUBLIC
    $<$<CONFIG:Debug,Release>:JPH_DEBUG_RENDERER>
    $<$<CONFIG:Debug,Release>:JPH_PROFILE_ENABLED>
    JPH_OBJECT_STREAM
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug,Release>>:JPH_FLOATING_POINT_EXCEPTIONS_ENABLED>
)

# All static dependencies (assimp, Jolt, angelscript, recast) are built with
# /MD (MultiThreadedDLL).  Force /MD here so CRT records match at link time.
if(MSVC AND NOT BUILD_SHARED_LIBS)
    set_target_properties(Kemena3DSDK PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# OpenGL context version — default 3.3, opt-in to 4.5
option(KEMENA_OPENGL_45 "Use an OpenGL 4.5 context instead of 3.3" OFF)
if(KEMENA_OPENGL_45)
    target_compile_definitions(Kemena3DSDK PUBLIC KEMENA_OPENGL_45)
endif()

# DirectX 11 backend for Windows desktop.
# When enabled the engine links against d3d11, dxgi, and d3dcompiler,
# and the kDX11Driver class becomes available.  The OpenGL backend
# remains compiled alongside it; the renderer type is chosen at runtime.
option(KEMENA_D3D11 "Build with DirectX 11 backend (Windows only)" OFF)
if(KEMENA_D3D11)
    if(WIN32)
        target_compile_definitions(Kemena3DSDK PUBLIC KEMENA_D3D11)
        # kdx11driver.h includes <windows.h> and <d3d11.h>, so every consumer of
        # that header needs NOMINMAX and WIN32_LEAN_AND_MEAN defined *before* any
        # Windows header is pulled in — otherwise the min/max macros leak and
        # break std::min/std::max in translation units that include the STL after
        # them (observed in krenderer.cpp / koffscreenrenderer.cpp as soon as
        # KEMENA_D3D11 is enabled).  PUBLIC so the studio and runtime targets
        # inherit them, and only for this backend so OpenGL builds keep exactly
        # the definitions they had before.
        target_compile_definitions(Kemena3DSDK PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
        message(STATUS "Kemena3D SDK: DirectX 11 backend enabled")
    else()
        message(WARNING "KEMENA_D3D11 is only supported on Windows — disabling")
        set(KEMENA_D3D11 OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# OpenGL ES 3.0 backend wiring
#
# The engine compiles against <GLES3/gl3.h> instead of GLEW, requests an ES 3.0
# context, and routes the ES-only code paths through kOpenGLESDriver.  Which
# platforms are eligible (and why desktop is rejected) is decided near the top
# of this file.
# ---------------------------------------------------------------------------
if(KEMENA_HAS_GLES)
    target_compile_definitions(Kemena3DSDK PUBLIC KEMENA_GLES)

    # Explicit GLES/EGL include root for ANGLE, PowerVR or other vendor SDKs.
    if(KEMENA_GLES_INCLUDE_DIR)
        target_include_directories(Kemena3DSDK PUBLIC ${KEMENA_GLES_INCLUDE_DIR})
    endif()

    if(ANDROID)
        # GLESv3 and EGL ship inside the NDK sysroot.
        target_link_libraries(Kemena3DSDK PUBLIC GLESv3 EGL)
    elseif(IOS)
        # iOS provides OpenGLES.framework; there is no EGL (EAGL is used, and
        # SDL wraps it).  The framework is deprecated since iOS 12 but still
        # functional and is the only ES path available.
        find_library(OPENGLES_FRAMEWORK OpenGLES)
        if(OPENGLES_FRAMEWORK)
            target_link_libraries(Kemena3DSDK PUBLIC ${OPENGLES_FRAMEWORK})
            message(STATUS "Kemena3D SDK: linking OpenGLES.framework (${OPENGLES_FRAMEWORK})")
        else()
            message(FATAL_ERROR "OpenGLES.framework was not found for the iOS build.")
        endif()
    elseif(EMSCRIPTEN)
        # WebGL 2 is the browser's ES 3.0 equivalent; nothing extra to link.
        message(STATUS "Kemena3D SDK: using the WebGL 2 (ES 3.0) subset")
    else()
        # Embedded Linux and the explicit desktop-emulation path (ANGLE/Mesa).
        target_link_libraries(Kemena3DSDK PUBLIC GLESv3 EGL)
    endif()

    message(STATUS "Kemena3D SDK: OpenGL ES 3.0 backend enabled (${KEMENA_GLES_PLATFORM})")
endif()

# Platform specific
if (WIN32)
	target_link_libraries(Kemena3DSDK PUBLIC opengl32)
	   if(KEMENA_D3D11)
	       target_link_libraries(Kemena3DSDK PUBLIC d3d11 dxgi d3dcompiler)
	   endif()
	   target_compile_definitions(Kemena3DSDK PRIVATE _WINDOWS NOMINMAX)
	if (NOT BUILD_SHARED_LIBS)
		target_compile_definitions(Kemena3DSDK PUBLIC KEMENA3D_STATIC)
	endif()
	# Define export macro only for shared builds
    if (BUILD_SHARED_LIBS)
        target_compile_definitions(Kemena3DSDK PRIVATE KEMENA3D_EXPORTS)
    endif()
elseif (APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(Kemena3DSDK PUBLIC ${COCOA_LIBRARY} ${OpenGL_LIBRARY})
    target_compile_definitions(Kemena3DSDK PRIVATE _MACOS)
elseif (UNIX)
    find_package(OpenGL REQUIRED)
    find_package(X11 REQUIRED)
    target_link_libraries(Kemena3DSDK PUBLIC ${OPENGL_LIBRARIES} ${X11_LIBRARIES})
    target_compile_definitions(Kemena3DSDK PRIVATE _LINUX)
endif()

# Link libraries
target_link_libraries(Kemena3DSDK
    PUBLIC
		$<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/build_Debug/libangelscript.dll.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/build_Release/libangelscript.dll.a
        >
		$<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/sdl/build_Debug/libSDL3.dll.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/sdl/build_Release/libSDL3.dll.a
        >
)

# Assimp is linked only for editor-style builds.
if(KEMENA_USE_ASSIMP)
    target_link_libraries(Kemena3DSDK
        PUBLIC
            $<IF:$<CONFIG:Debug>,
                ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/assimp/build_Debug/libassimp.dll.a,
                ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/assimp/build_Release/libassimp.dll.a
            >
    )
endif()

# GLEW (built from source via build/cmake/)
# Skipped entirely for ES builds: mobile targets load GLES entry points from
# their platform loader, and GLEW is not cross-compiled for Android/iOS.
if(NOT KEMENA_HAS_GLES)
    if(MSVC)
        target_link_libraries(Kemena3DSDK PUBLIC
            $<IF:$<CONFIG:Debug>,
                ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Debug/lib/Debug/${GLEW_LIB}.lib,
                ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Release/lib/Release/${GLEW_LIB}.lib
            >
        )
    else()
        target_link_libraries(Kemena3DSDK PUBLIC
            $<IF:$<CONFIG:Debug>,
                ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Debug/lib/lib${GLEW_LIB}.a,
                ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Release/lib/lib${GLEW_LIB}.a
            >
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Audio codec libraries (libogg, libvorbis, libvorbisfile, libopus, libopusfile)
# Built from source by Kemena3D-SDK/download_dep.py.
# ---------------------------------------------------------------------------
set(OGG_ROOT        ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/ogg)
set(VORBIS_ROOT     ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/vorbis)
set(OPUS_ROOT       ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/opus)
set(OPUSFILE_ROOT   ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/opusfile)

if(MSVC)
    # MSVC multi-config → libs land in build_<Config>/<Config>/ (ogg, opus, opusfile)
    # or build_<Config>/lib/<Config>/ (vorbis which sets ARCHIVE_OUTPUT_DIRECTORY).
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${OGG_ROOT}/build_Debug/Debug/ogg.lib,
            ${OGG_ROOT}/build_Release/Release/ogg.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${VORBIS_ROOT}/build_Debug/lib/Debug/vorbis.lib,
            ${VORBIS_ROOT}/build_Release/lib/Release/vorbis.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${VORBIS_ROOT}/build_Debug/lib/Debug/vorbisfile.lib,
            ${VORBIS_ROOT}/build_Release/lib/Release/vorbisfile.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${OPUS_ROOT}/build_Debug/Debug/opus.lib,
            ${OPUS_ROOT}/build_Release/Release/opus.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${OPUSFILE_ROOT}/build_Debug/Debug/opusfile.lib,
            ${OPUSFILE_ROOT}/build_Release/Release/opusfile.lib
        >
    )
else()
    # MinGW / Unix single-config → libs land directly in build_<Config>/
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${OGG_ROOT}/build_Debug/libogg.a,
            ${OGG_ROOT}/build_Release/libogg.a
        >
        $<IF:$<CONFIG:Debug>,
            ${VORBIS_ROOT}/build_Debug/lib/libvorbis.a,
            ${VORBIS_ROOT}/build_Release/lib/libvorbis.a
        >
        $<IF:$<CONFIG:Debug>,
            ${VORBIS_ROOT}/build_Debug/lib/libvorbisfile.a,
            ${VORBIS_ROOT}/build_Release/lib/libvorbisfile.a
        >
        $<IF:$<CONFIG:Debug>,
            ${OPUS_ROOT}/build_Debug/libopus.a,
            ${OPUS_ROOT}/build_Release/libopus.a
        >
        $<IF:$<CONFIG:Debug>,
            ${OPUSFILE_ROOT}/build_Debug/libopusfile.a,
            ${OPUSFILE_ROOT}/build_Release/libopusfile.a
        >
    )
endif()

# Jolt Physics (always static; output path differs between generators)
#   MSVC:   build_Debug/Debug/Jolt.lib
#   Xcode:  build_Debug/Debug/libJolt.a
#   Unix/Makefiles:  build_Debug/libJolt.a
if(MSVC)
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/Debug/Jolt.lib,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/Release/Jolt.lib
        >
    )
elseif(CMAKE_GENERATOR STREQUAL "Xcode")
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/Debug/libJolt.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/Release/libJolt.a
        >
    )
else()
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/libJolt.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/libJolt.a
        >
    )
endif()

# Recast Navigation (Recast, Detour, DetourCrowd, DetourTileCache)
if(MSVC)
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/Recast.lib,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/Recast.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/Detour.lib,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/Detour.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/DetourCrowd.lib,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/DetourCrowd.lib
        >
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/DetourTileCache.lib,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/DetourTileCache.lib
        >
    )
else()
    target_link_libraries(Kemena3DSDK PUBLIC
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libRecast.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libRecast.a
        >
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetour.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetour.a
        >
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetourCrowd.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetourCrowd.a
        >
        $<IF:$<CONFIG:Debug>,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetourTileCache.a,
            ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetourTileCache.a
        >
    )
endif()

# Suppress some noisy warnings
if (MSVC)
    target_compile_options(Kemena3DSDK PRIVATE /wd4996 /FS)
    # Treat source and execution charsets as UTF-8. Without this MSVC assumes
    # the active code page (e.g. 936/GBK) and emits C4819 for UTF-8 files
    # containing non-ASCII characters (e.g. ©, –, emoji in comments).
    target_compile_options(Kemena3DSDK PRIVATE /utf-8)
else()
    target_compile_options(Kemena3DSDK PRIVATE -Wno-deprecated-declarations)
endif()

# Visual Studio startup project
if (MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Kemena3DSDK)
endif()

include(GNUInstallDirs) # defines CMAKE_INSTALL_INCLUDEDIR=include, LIBDIR=lib

# Install libraries → into lib/<CONFIG>
install(TARGETS Kemena3DSDK
    EXPORT Kemena3DSDKTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION lib
    PUBLIC_HEADER DESTINATION include/kemena
)

# Headers (always into Output/include)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imgui/backends/
    DESTINATION include/backends FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/include/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/add_on/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/sdl/include/SDL3/
    DESTINATION include/SDL3 FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/include/GL/
    DESTINATION include/GL FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/assimp/include/assimp/
    DESTINATION include/assimp FILES_MATCHING PATTERN "*.h*" PATTERN "*.inl"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glm/glm/
    DESTINATION include/glm FILES_MATCHING PATTERN "*.h*" PATTERN "*.inl"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/stb/
    DESTINATION include/stb FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/nlohmann/include/nlohmann/
    DESTINATION include/nlohmann FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/portable-file-dialogs/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/imguizmo/src/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/slang/build_Release/dist/include/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/miniaudio/
    DESTINATION include FILES_MATCHING PATTERN "*.h*"
)
# Audio codec headers (ogg, vorbis, opus, opusfile)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/ogg/include/
    DESTINATION include/ogg FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/vorbis/include/
    DESTINATION include/vorbis FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/opus/include/
    DESTINATION include/opus FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/opusfile/include/
    DESTINATION include/opus FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Jolt/
    DESTINATION include/Jolt FILES_MATCHING PATTERN "*.h*"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/recast/dist/include/
    DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# Dependency libs (per-config into Output/lib/{Debug,Release})
file(GLOB_RECURSE LIBS_DEBUG
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/**/build_Debug/*.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/**/build_Debug/*.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/**/build_Debug/*.dll"
	"${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/projects/**/build_Debug/*.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/projects/**/build_Debug/*.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/projects/**/build_Debug/*.dll"
)
file(GLOB_RECURSE LIBS_RELEASE
	"${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/**/build_Release/*.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/**/build_Release/*.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/**/build_Release/*.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/projects/**/build_Release/*.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/projects/**/build_Release/*.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/angelscript/angelscript/projects/**/build_Release/*.dll"
)
install(FILES ${LIBS_DEBUG}   DESTINATION lib   CONFIGURATIONS Debug)
install(FILES ${LIBS_RELEASE} DESTINATION lib	CONFIGURATIONS Release)

file(GLOB GLEW_LIBS_DEBUG
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Debug/lib/Debug/*.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Debug/lib/*.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Debug/bin/Debug/*.dll"
)
file(GLOB GLEW_LIBS_RELEASE
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Release/lib/Release/*.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Release/lib/*.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/glew/build/cmake/build_Release/bin/Release/*.dll"
)
install(FILES ${GLEW_LIBS_DEBUG}   DESTINATION lib CONFIGURATIONS Debug)
install(FILES ${GLEW_LIBS_RELEASE} DESTINATION lib CONFIGURATIONS Release)

# Jolt Physics — output path varies by generator:
#   MSVC:   build_Debug/Debug/Jolt.lib
#   Xcode:  build_Debug/Debug/libJolt.a
#   Unix/Makefiles:  build_Debug/libJolt.a
if(MSVC)
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/Debug/Jolt.lib"
        DESTINATION lib CONFIGURATIONS Debug
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/Release/Jolt.lib"
        DESTINATION lib CONFIGURATIONS Release
    )
elseif(CMAKE_GENERATOR STREQUAL "Xcode")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/Debug/libJolt.a"
        DESTINATION lib CONFIGURATIONS Debug
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/Release/libJolt.a"
        DESTINATION lib CONFIGURATIONS Release
    )
else()
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/libJolt.a"
        DESTINATION lib CONFIGURATIONS Debug
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/libJolt.a"
        DESTINATION lib CONFIGURATIONS Release
    )
endif()