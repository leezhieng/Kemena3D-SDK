# Demos

Standalone example programs that link against the built Kemena3D SDK.

| Demo | Description | Screenshot |
|------|-------------|------------|
| [1. Hello World](1.%20Hello%20World/) | Create a window and camera, load a 3D model with a textured flat material, and render it in a loop. | <img src="1. Hello World/screenshot.png" alt="Hello World" width="300"/> |

## Prerequisites

Build the SDK first, so the demos can link against the headers and libraries
under `Kemena3D-SDK/Output/<Config>/`:

```sh
cd Kemena3D-SDK
python download_dep.py   # fetch + build third-party dependencies (once per machine)
python build_sdk.py      # build + install the SDK into Output/Debug and Output/Release
```

## Building a demo

Each demo is a standalone CMake project. Open the demo's folder and configure an
out-of-source build, then build it. The executable links statically against
`Output/<Config>/{include,lib}`, so build it in the same configuration you want
to run.

### Windows (Visual Studio 2026)

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```
Output: `build\bin\Release\`. Release builds are windowed (no console); Debug
keeps a console for diagnostics.

### Linux

Requires the SDK built for Linux plus OpenGL/X11 dev packages
(e.g. `libgl1-mesa-dev`, `libx11-dev` on Debian/Ubuntu).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Output: `build/bin/`.

### FreeBSD

Install the OpenGL/X11 dev packages (e.g. `pkg install mesa-libs libX11`), then
build the same way as Linux.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Output: `build/bin/`.

> **Notes**
> - Run each demo from its build-output folder so the relative asset paths
>   (model, texture, shader) resolve. See the demo's own README for details.
> - Windows is the fully verified path. Linux/FreeBSD are best-effort while the
>   dependency build for those platforms is finalized — if linking fails, check
>   the static-library names in `Output/<Config>/lib` against the demo's
>   `CMakeLists.txt`. macOS is not configured yet.
