# Building

## Dependencies

| | why | how |
|---|---|---|
| CMake ≥ 3.21 | build system | |
| A C++20 compiler | | GCC 11+, Clang 14+, MSVC 2022 |
| SDL2 ≥ 2.0.5 | window, input, GL context | `SDL_GetWindowBordersSize` needs 2.0.5 |
| OpenGL 3.3 core | presentation | driver, no SDK needed |
| Slint ≥ 1.17 (C++) | UI | `scripts/fetch_slint.*` |

No Rust toolchain is needed: the fetch scripts download Slint's prebuilt C++ SDK.

## Linux

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev libgl1-mesa-dev libinput10
./scripts/fetch_slint.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/swos-launcher
```

`libinput10` is not used by the launcher — it is a link-time dependency of the
prebuilt `libslint_cpp.so`, which is built with Slint's LinuxKMS backend enabled.
Without it the link fails with `undefined reference to libinput_*`.

## macOS

```bash
brew install cmake ninja sdl2
./scripts/fetch_slint.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Apple Silicon only** for the prebuilt route: Slint publishes no `Darwin-x86_64`
package. On an Intel Mac, install Rust and build Slint from source (below).

## Windows

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch_slint.ps1
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

SDL2 via `vcpkg install sdl2:x64-windows`. The prebuilt Slint package is an MSVC
build, so the launcher must be built with MSVC too — for MinGW, build Slint from
source.

## Building Slint from source

Needed for Intel macOS, MinGW, or any platform without a prebuilt package.

```bash
rustup default stable   # https://rustup.rs
git clone --depth 1 --branch v1.17.1 https://github.com/slint-ui/slint
cmake -S slint -B slint-build -DCMAKE_INSTALL_PREFIX=$PWD/third_party/slint \
      -DSLINT_FEATURE_RENDERER_SOFTWARE=ON -DBUILD_SHARED_LIBS=ON
cmake --build slint-build --target install
```

The software renderer is the one feature the launcher genuinely requires; the
CMake configure step fails with a clear message if it is missing.

## Useful CMake options

| option | meaning |
|---|---|
| `-DSlint_DIR=<prefix>/lib/cmake/Slint` | use a Slint outside `third_party/slint` |
| `-DSWOS_SLINT_ROOT=<prefix>` | same, given as the SDK root |
| `-DCMAKE_BUILD_TYPE=Debug` | default is Release |

## Smoke test

The UI can be verified without a visible desktop — useful in CI and under WSLg,
where the compositor cannot be screenshotted:

```bash
SWOS_LAUNCHER_TAB=1 SWOS_LAUNCHER_SCREENSHOT=/tmp/ui.bmp ./build/bin/swos-launcher
```

This opens the Display tab, writes the first settled frame to a BMP and exits.
See the debug-hooks table in `ARCHITECTURE.md`.
