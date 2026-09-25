import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

# Map compiler choice → CMake generator name
COMPILER_GENERATOR = {
    "1": "Visual Studio 18 2026",
    "2": "Visual Studio 17 2022",
    "3": "MinGW Makefiles",
}

# Resolve project root from script location so the script works
# regardless of which directory the user runs it from.
ROOT = Path(__file__).resolve().parent

MINGW_SEARCH_PATHS = [
    r"C:\mingw64\bin",
    r"C:\mingw32\bin",
    r"C:\mingw\bin",
    r"C:\msys64\mingw64\bin",
    r"C:\msys64\mingw32\bin",
    r"C:\msys64\usr\bin",
    r"C:\msys2\mingw64\bin",
    r"C:\msys2\mingw32\bin",
]

def find_mingw_make():
    """Return the path to mingw32-make or make, searching PATH then known install dirs."""
    for name in ("mingw32-make", "make"):
        found = shutil.which(name)
        if found:
            return found
    for directory in MINGW_SEARCH_PATHS:
        for name in ("mingw32-make.exe", "make.exe"):
            candidate = os.path.join(directory, name)
            if os.path.isfile(candidate):
                return candidate
    raise RuntimeError(
        "Could not find mingw32-make or make. "
        "Ensure MinGW bin directory is on PATH or install MinGW to a standard location."
    )

def run_cmd(cmd, cwd=None):
    print(f"[RUN] {cmd}")
    result = subprocess.run(cmd, shell=True, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(f"[ERROR] Command failed: {cmd}")

def banner():
    print(r"""
  _  __   ___   __  __    ___    _  _     ___     ____    ___
 | |/ /  | __| |  \/  |  | __|  | \| |   /   \   |__ /   |   \
 | ' <   | _|  | |\/| |  | _|   | .` |   | - |    |_ \   | |) |
 |_|\_\  |___| |_|  |_|  |___|  |_|\_|   |_|_|   |___/   |___/
                        www.kemena3d.com
 ------------------------------------------------------------------------
 Automatically compile Kemena3D SDK...
 ------------------------------------------------------------------------
""")

def choose(prompt, options: dict, env=None):
    # Non-interactive override: if the given environment variable holds a valid
    # option key, use it without prompting (used by CI).
    if env:
        val = os.environ.get(env, "").strip()
        if val in options:
            print(f"{prompt}\n  [{env}={val}] (non-interactive)")
            return val
    print(prompt)
    for k, v in options.items():
        print(f"{k}: {v}")
    choice = input("Enter your choice: ").strip()
    if choice not in options:
        raise ValueError(f"Invalid choice: {choice}")
    return choice

def is_multi_config(generator):
    """Return True for generators that select the config at build time (VS, Xcode)."""
    return generator.startswith("Visual Studio") or generator == "Xcode"

def build_with_cmake(generator, build_mode, args, make_program=None):
    build_dir = ROOT / f"build_{build_mode}"
    install_prefix = ROOT / f"Output/{build_mode}"

    make_arg = f'-DCMAKE_MAKE_PROGRAM="{make_program}" ' if make_program else ""

    # Only single-config generators (Unix Makefiles, MinGW Makefiles) need
    # CMAKE_BUILD_TYPE at configure time; multi-config ones use --config at build.
    build_type_arg = "" if is_multi_config(generator) else f"-DCMAKE_BUILD_TYPE={build_mode} "

    # Configure
    run_cmd(
        f'cmake -S "{ROOT}" -B "{build_dir}" -G "{generator}" '
        f'{make_arg}'
        f'-DCMAKE_INSTALL_PREFIX="{install_prefix}" '
        f'{build_type_arg}{args}'
    )

    # Build
    run_cmd(f'cmake --build "{build_dir}" --config {build_mode} --parallel')

    # Install
    run_cmd(f'cmake --install "{build_dir}" --config {build_mode}')

    print("[SUCCESS] Kemena3D SDK built and installed successfully.")

def main():
    banner()
    system = platform.system()

    # Compiler selection
    if system == "Windows":
        compiler = choose(
            "\nPlease choose a compiler:",
            {
                "1": "Build with Visual Studio 2026 (Community Edition)",
                "2": "Build with Visual Studio 2022 (Community Edition)",
                "3": "Build with MinGW (GCC 14 or above)"
            },
            env="KEMENA_COMPILER"
        )
    elif system == "Linux":
        print("\nLinux detected → using GCC (default).")
        compiler = "1"
    elif system == "FreeBSD":
        print("\nFreeBSD detected → using GCC (default).")
        compiler = "1"
    elif system == "Darwin":  # macOS
        compiler = choose(
            "\nPlease choose a compiler:",
            {
                "1": "Xcode (Clang/LLVM from Command Line Tools)",
                "2": "GCC (via Homebrew or custom install)"
            },
            env="KEMENA_COMPILER"
        )
    else:
        print(f"Unsupported platform: {system}")
        sys.exit(1)

    # Linking selection
    linking = choose(
        "\nPlease choose static linking or dynamic linking:",
        {"1": "Static linking (library built into executable)",
         "2": "Dynamic linking (DLL / shared library)"},
        env="KEMENA_LINKING"
    )

    # Assimp toggle — when off, tinygltf is the only importer (glTF/GLB only,
    # no save). Use the slim build for Kemena3D-Runtime; the editor needs ON.
    assimp = choose(
        "\nInclude Assimp for full-format mesh import?",
        {"1": "Yes — full importer + .glb export (editor / general use)",
         "2": "No  — slim build: tinygltf-only, glTF/GLB load (runtime use)"},
        env="KEMENA_USE_ASSIMP"
    )
    use_assimp_flag = "ON" if assimp == "1" else "OFF"

    # Renderer selection — platform-dependent
    use_d3d11_flag = "OFF"
    use_gles_flag = "OFF"
    use_gl45_flag = "OFF"

    if system == "Windows":
        renderer = choose(
            "\nSelect graphics renderers to include:",
            {
                "1": "OpenGL 3.3 only (default, always available)",
                "2": "OpenGL 3.3 + DirectX 11 (Windows desktop)"
            },
            env="KEMENA_RENDERER"
        )
        if renderer == "2":
            use_d3d11_flag = "ON"
    elif system in ("Linux", "FreeBSD"):
        print("\nLinux/FreeBSD detected → OpenGL 3.3 renderer (default).")
    elif system == "Darwin":
        print("\nmacOS detected → OpenGL 3.3 renderer (default).")
    else:
        print(f"\n{system} detected → OpenGL 3.3 renderer (default).")

    # Build configuration selection
    config = choose(
        "\nPlease choose a build configuration:",
        {
            "1": "Debug",
            "2": "Release",
            "3": "Both (Debug and Release)"
        },
        env="KEMENA_CONFIG"
    )

    # CMake generator setup
    make_program = None
    if system == "Windows":
        if compiler in ("1", "2"):
            generator = COMPILER_GENERATOR.get(compiler, "Visual Studio 18 2026")
        elif compiler == "3":
            generator = "MinGW Makefiles"
            make_program = find_mingw_make()
            print(f"[INFO] Using make program: {make_program}")
    elif system in ("Linux", "FreeBSD"):
        generator = "Unix Makefiles"
    elif system == "Darwin":
        generator = "Xcode" if compiler == "1" else "Unix Makefiles"
    else:
        raise RuntimeError(f"No CMake generator for {system}/{compiler}")

    # Build args
    if system == "Windows":
        if compiler in ("1", "2"):  # Visual Studio
            if linking == "1":
                args = "-DBUILD_SHARED_LIBS=OFF -DUSE_MINGW=OFF"
            else:
                args = "-DBUILD_SHARED_LIBS=ON -DUSE_MINGW=OFF -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON -DKEMENA_SHARED=ON"
        else:  # MinGW
            if linking == "1":
                args = "-DBUILD_SHARED_LIBS=OFF -DUSE_MINGW=ON"
            else:
                args = "-DBUILD_SHARED_LIBS=ON -DUSE_MINGW=ON -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON -DKEMENA_SHARED=ON"

    elif system in ("Linux", "FreeBSD", "Darwin"):
        if linking == "1":
            args = "-DBUILD_SHARED_LIBS=OFF"
        else:
            args = "-DBUILD_SHARED_LIBS=ON -DKEMENA_SHARED=ON"
    else:
        raise RuntimeError(f"No build args defined for {system}")

    args += f" -DKEMENA_USE_ASSIMP={use_assimp_flag}"
    args += f" -DKEMENA_D3D11={use_d3d11_flag}"
    args += f" -DKEMENA_GLES={use_gles_flag}"
    args += f" -DKEMENA_OPENGL_45={use_gl45_flag}"

    configs = []
    if config == "1":
        configs = ["Debug"]
    elif config == "2":
        configs = ["Release"]
    else:
        configs = ["Debug", "Release"]

    for cfg in configs:
        build_with_cmake(generator, cfg, args, make_program)

    print("\n------------------------------------------------------------------------")
    print("Kemena3D SDK has been compiled successfully.")
    print("------------------------------------------------------------------------")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("\n------------------------------------------------------------------------")
        print(f"Failed to compile Kemena3D SDK: {e}")
        print("------------------------------------------------------------------------")
        sys.exit(1)
