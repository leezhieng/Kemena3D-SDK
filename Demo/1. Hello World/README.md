# Hello World

The simplest Kemena3D program. It creates a window and renderer, sets up a
world / scene / camera, loads the **flat (unlit) shader** and a texture into a
material, loads a 3D model, applies the material, and renders it in a loop until
the window is closed.

See [`main.cpp`](main.cpp) — it's intentionally short and commented.

## Prerequisites

Build the Kemena3D SDK first, so the headers and libraries exist under
`Kemena3D-SDK/Output/<Config>/`:

```sh
cd Kemena3D-SDK
python download_dep.py   # fetch + build third-party dependencies (once per machine)
python build_sdk.py      # build + install the SDK into Output/Debug and Output/Release
```

The demo links statically against `Output/<Config>/{include,lib}`, so it picks
up whichever configuration (Debug / Release) you build it in.

## Build & run

The model, texture and shader are referenced with **relative paths** (e.g.
`../diffuse.png`, `../flat.glsl`), so run the executable
from its build output folder so those paths resolve.

### Windows (Visual Studio 2026)

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
cd build\bin\Release
demo.exe
```

Release builds as a windowed app (no console); Debug keeps a console for
diagnostics.

### Linux

Requires the SDK built for Linux (GCC/Clang) plus OpenGL and X11 development
packages (e.g. `libgl1-mesa-dev`, `libx11-dev` on Debian/Ubuntu).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build/bin
./demo
```

### FreeBSD

Install the OpenGL/X11 dev packages (e.g. `pkg install mesa-libs libX11`), then
build the same way as Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build/bin
./demo
```

> **Note (Linux/FreeBSD):** these targets are best-effort. The dependency build
> on those platforms is still being finalized, and the static-library names in
> `CMakeLists.txt` follow the project's GCC/Clang build. If linking fails, check
> the actual `.a` names in `Output/<Config>/lib` and adjust the `elseif(UNIX)`
> block to match. Windows is the fully verified path.

## 3D Model

RPG Reptile Mage
- Downloaded from https://sketchfab.com/3d-models/rpg-reptile-mage-0641cc113cfb4a6ba7269aa696ae1512
- Removed the ground plane and adjusted the pivot point in Blender

## Screenshot

![Screenshot](screenshot.png)
