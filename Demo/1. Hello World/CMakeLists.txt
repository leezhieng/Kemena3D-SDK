cmake_minimum_required(VERSION 3.15)

project(Kemena3dDemo LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Match the CRT the SDK and its dependencies (assimp, Jolt, ...) were built
# with: /MD (MultiThreadedDLL). CMP0091=NEW enables CMAKE_MSVC_RUNTIME_LIBRARY.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Output to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Source file
add_executable(demo main.cpp)

# Link statically against the Kemena3D SDK.
target_compile_definitions(demo PRIVATE KEMENA3D_STATIC)

# The built SDK output now ships headers and libs split by build config
# (Debug / Release). Everything the demo needs - Kemena3DSDK, glm, nlohmann, and
# all third-party libs (SDL3, assimp, AngelScript, GLEW, zlib, Recast/Detour,
# Jolt) — lives under Output/<Config>/.
set(KEMENA3D_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../../Output")

target_include_directories(demo PRIVATE
    "$<$<CONFIG:Debug>:${KEMENA3D_OUTPUT}/Debug/include>"
    "$<$<CONFIG:Release>:${KEMENA3D_OUTPUT}/Release/include>"
)
target_link_directories(demo PRIVATE
    "$<$<CONFIG:Debug>:${KEMENA3D_OUTPUT}/Debug/lib>"
    "$<$<CONFIG:Release>:${KEMENA3D_OUTPUT}/Release/lib>"
)

# Release builds use the GUI subsystem (no console window); Debug keeps the
# console for diagnostics — same convention as the editor.
set_target_properties(demo PROPERTIES WIN32_EXECUTABLE "$<$<CONFIG:Release>:TRUE>")

# Link libraries (Windows only; Linux/macOS link lists are not yet configured)
if(WIN32)
    target_link_libraries(demo PRIVATE
        Kemena3DSDK

        # Third-party static libraries (MSVC names; Debug variants use a suffix)
        SDL3-static
        $<$<CONFIG:Debug>:assimp-vc${MSVC_TOOLSET_VERSION}-mtd>
        $<$<CONFIG:Release>:assimp-vc${MSVC_TOOLSET_VERSION}-mt>
        $<$<CONFIG:Debug>:angelscriptd>
        $<$<CONFIG:Release>:angelscript>
        imgui
        imguizmo
        $<$<CONFIG:Debug>:libglew32d>
        $<$<CONFIG:Release>:libglew32>
        $<$<CONFIG:Debug>:zlibstaticd>
        $<$<CONFIG:Release>:zlibstatic>
        # Recast Navigation (Debug names carry a -d suffix)
        $<$<CONFIG:Debug>:Recast-d>
        $<$<CONFIG:Debug>:Detour-d>
        $<$<CONFIG:Debug>:DetourCrowd-d>
        $<$<CONFIG:Debug>:DetourTileCache-d>
        $<$<CONFIG:Release>:Recast>
        $<$<CONFIG:Release>:Detour>
        $<$<CONFIG:Release>:DetourCrowd>
        $<$<CONFIG:Release>:DetourTileCache>
        # Jolt Physics (same lib name in both config output folders)
        Jolt

        # Windows system libraries
        opengl32
        winmm
        ole32
        setupapi
        gdi32
        imm32
        version
        oleaut32
        uuid
    )

    # WIN32_EXECUTABLE uses /SUBSYSTEM:WINDOWS which expects WinMain; SDL is built
    # with SDL_MAIN_HANDLED so the app keeps a standard int main() — point the
    # Release entry at the C runtime startup that calls it.
    target_link_options(demo PRIVATE
        /IGNORE:4099
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
    )

elseif(APPLE)
    # macOS isn't wired up in the dependency build yet.
    message(WARNING "Kemena3D Hello World demo: macOS link list is not configured.")
    target_link_libraries(demo PRIVATE Kemena3DSDK)

elseif(UNIX)
    # Linux / FreeBSD. Best-effort: the SDK and its dependencies must have been
    # built for this platform (download_dep.py + build_sdk.py) and installed into
    # Output/<Config>/{include,lib} as static archives. Library names here follow
    # the project's GCC/Clang build; if your built artifacts differ, adjust them
    # to match the .a files in Output/<Config>/lib.
    find_package(OpenGL REQUIRED)
    find_package(X11 REQUIRED)

    # Static archives have order-sensitive, sometimes circular references; wrap
    # the engine + its vendored deps in a link group so GNU/LLVM ld resolves them.
    target_link_libraries(demo PRIVATE
        -Wl,--start-group
            Kemena3DSDK
            SDL3
            assimp
            angelscript
            imgui
            imguizmo
            GLEW
            zlibstatic
            Recast
            Detour
            DetourCrowd
            DetourTileCache
            Jolt
        -Wl,--end-group
        # System libraries
        OpenGL::GL
        ${X11_LIBRARIES}
        ${CMAKE_DL_LIBS}
        pthread
        m
    )
endif()
