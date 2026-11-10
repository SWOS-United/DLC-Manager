# Building

## Dependencies

| | why | how |
|---|---|---|
| CMake ≥ 3.21 | build system | |
| A C++20 compiler | | GCC 11+, Clang 14+, MSVC 2022 |
| SDL2 ≥ 2.0.5 | window, input, GL context | `SDL_GetWindowBordersSize` needs 2.0.5 |
| OpenGL 3.3 core | presentation | driver, no SDK needed |
| Slint ≥ 1.17 (C++) | UI | `scripts/fetch_slint.*` |
| libcurl | DLC downloads | already in the macOS SDK; package needed on Linux/Windows |
| libarchive | 7z extraction | needs liblzma, pulled in automatically. **Also needed on macOS** -- Apple ships the dylib but no headers |
| SDL2_mixer | DLC audio preview | **optional** — without it the DLC manager builds and runs, it just cannot play a sample |

No Rust toolchain is needed: the fetch scripts download Slint's prebuilt C++ SDK.

## Linux

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev libgl1-mesa-dev \
                 libinput10 libcurl4-openssl-dev libarchive-dev libsdl2-mixer-dev
./scripts/fetch_slint.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/swos-dlc-manager
```

`libinput10` is not used by the DLC Manager — it is a link-time dependency of the
prebuilt `libslint_cpp.so`, which is built with Slint's LinuxKMS backend enabled.
Without it the link fails with `undefined reference to libinput_*`.

## macOS

```bash
brew install cmake ninja sdl2 libarchive sdl2_mixer
./scripts/fetch_slint.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`libcurl` comes from the macOS SDK, so it needs no package. `libarchive` does:
Apple ships `libarchive.dylib` but no public headers, so `find_package` fails
without the brew formula. It installs keg-only, and CMake locates it through
`brew --prefix`, so a plain `brew install` is enough.

Verified on macOS 26.6 (Apple Silicon, Xcode clang 21): both apps build as `.app`
bundles and run, with the `.icns` generated at configure time by `iconutil`.

**Apple Silicon only** for the prebuilt route: Slint publishes no `Darwin-x86_64`
package. On an Intel Mac, install Rust and build Slint from source (below).

## Windows

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch_slint.ps1
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Dependencies via `vcpkg install sdl2 curl libarchive sdl2-mixer --triplet x64-windows`. The prebuilt Slint package is an MSVC
build, so the app must be built with MSVC too — for MinGW, build Slint from
source.

For one `.exe` with no DLLs beside it but Slint's, use the static triplet:
`--triplet x64-windows-static` for the libraries, and
`-DVCPKG_TARGET_TRIPLET=x64-windows-static` when configuring. CMake then links
vcpkg's static SDL2 and SDL2_mixer (`SDL2::SDL2-static`) and builds with the
static C runtime (`/MT`) to match them.

## Building Slint from source

Needed for Intel macOS, MinGW, or any platform without a prebuilt package.

```bash
rustup default stable   # https://rustup.rs
git clone --depth 1 --branch v1.17.1 https://github.com/slint-ui/slint
cmake -S slint -B slint-build -DCMAKE_INSTALL_PREFIX=$PWD/third_party/slint \
      -DSLINT_FEATURE_RENDERER_SOFTWARE=ON -DBUILD_SHARED_LIBS=ON
cmake --build slint-build --target install
```

The software renderer is the one feature the app genuinely requires; the
CMake configure step fails with a clear message if it is missing.

## Useful CMake options

| option | meaning |
|---|---|
| `-DSlint_DIR=<prefix>/lib/cmake/Slint` | use a Slint outside `third_party/slint` |
| `-DSWOS_SLINT_ROOT=<prefix>` | same, given as the SDK root |
| `-DCMAKE_BUILD_TYPE=Debug` | default is Release |
| `-DSWOS_USER_DIR=SWOSUnited` | the player's folder on Windows and macOS (OpenSWOS's: `SWOS`) |
| `-DSWOS_USER_DOT_DIR=.swosunited` | the player's folder on Linux (OpenSWOS's: `.swos`) |
| `-DCMAKE_MSVC_RUNTIME_LIBRARY=...` | overrides the C runtime a static vcpkg triplet picks |

`swos-dlc-manager --version` prints the version (the project's, in `CMakeLists.txt`).

## Smoke test

The UI can be verified without a visible desktop — useful in CI and under WSLg,
where the compositor cannot be screenshotted:

```bash
# A real catalogue refresh and install, headless:
SWOS_DLC_REFRESH=1 SWOS_DLC_CATEGORY=5 SWOS_DLC_DOWNLOAD=2 \
    SWOS_DLC_SCREENSHOT=/tmp/dlc.bmp ./build/bin/swos-dlc-manager
```

Each writes the first settled frame to a BMP and exits. See the debug-hooks
table in `ARCHITECTURE.md`.
