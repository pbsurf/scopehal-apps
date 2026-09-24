\page windows_msvc Native Windows build with MSVC (work in progress)

# Native Windows build with MSVC (work in progress)

The supported Windows build uses MSYS2 (see `msys2/PKGBUILD`). This page tracks an in-progress port to a native MSVC
toolchain with dependencies from [vcpkg](https://vcpkg.io), so that a Windows build needs only Visual Studio and a git
clone: no MSYS2, no manually installed Vulkan SDK or libiio.

**Status:** dependency manifest and CMake presets are in place but have not yet been tried on Windows. The main
CMake files and sources still assume GCC/Clang, so the build is expected to fail until the items under "Remaining
work" are done.

# Setup

1. Install Visual Studio 2022 or later (or the Build Tools) with the "Desktop development with C++" workload. This
   includes CMake, Ninja, and a copy of vcpkg.
2. Open a "Developer PowerShell / Command Prompt for VS" (x64). It sets `VCPKG_ROOT` to the bundled vcpkg; if you use
   a standalone vcpkg clone instead, set `VCPKG_ROOT` to it.
3. From the scopehal-apps checkout (with submodules):

       cmake --preset windows-msvc
       cmake --build --preset windows-msvc

   Other presets: `windows-msvc-debug`, and `windows-msvc-tests` (adds Catch2/FFTW and `BUILD_TESTING=ON`; run with
   `ctest --preset windows-msvc-tests`).

The first configure builds every dependency from source (glslang, shaderc and SDL2 are the slow ones), which can take
a long time. vcpkg's binary cache makes subsequent configures fast.

# How dependencies are provided

- `msvc/vcpkg.json`: the manifest (the presets point vcpkg at it with `VCPKG_MANIFEST_DIR`). The `builtin-baseline` pins every port version to one vcpkg commit; bump it
  deliberately. Features: `iio` (default, libiio for IIO SDRs) and `tests` (Catch2, FFTW; FFTW is GPL and only
  used by the unit tests).
- The whole Vulkan stack comes from vcpkg (`vulkan-headers`, `vulkan-loader`, `glslang`, `spirv-tools`, `shaderc`,
  which also provides `glslc`), so the LunarG Vulkan SDK is not needed. Take glslang/shaderc/SPIRV-Tools either all
  from vcpkg or all from the SDK, never mixed. The existing `VULKAN_SDK` code path in `CMakeLists.txt` is still
  there for anyone who prefers the SDK.
- The manifest's embedded `vcpkg-configuration` adds `msvc/ports/` as an overlay port directory.
- `msvc/ports/libiio`: an overlay port, since vcpkg has no libiio port. It builds **libiio v0.26**, the last 0.x
  release; scopehal only supports the 0.x API, and libiio 1.0 / upstream `main` changed the API and ABI. The port
  also fixes two upstream packaging problems on MSVC: the `.pc` file says `-liio` while the import library is
  named `libiio.lib`, and static builds need `LIBIIO_STATIC` defined for consumers. Static libiio's `.pc` file
  lacks its private dependencies (libxml2, libusb, ws2_32), so stick to the default dynamic `x64-windows` triplet.
- `CMakePresets.json` (it has to be in the source root, since that's the only place CMake looks for presets) points `PKG_CONFIG_EXECUTABLE` at vcpkg's pkgconf and `Vulkan_GLSLC_EXECUTABLE` at vcpkg's
  glslc, both under `<build>/vcpkg_installed/x64-windows/tools/`. If vcpkg changes that layout, fix the paths there.

# Remaining work

Build system:

- `CMakeLists.txt`: `-mtune=native`, `-O3`, `-Og`, `-g`, `-D_DEBUG` are added unconditionally, and
  `-D_USE_MATH_DEFINES -D_POSIX_THREAD_SAFE_FUNCTIONS` use GCC syntax. Guard them for MSVC (`/O2`, `/Zi`, ...).
- `lib/xptools/CMakeLists.txt`: on Windows, hidapi's pkg-config module is named `hidapi-winapi`, not `hidapi` or
  `hidapi-hidraw`. Add it to the search, or use hidapi's CMake package (`find_package(hidapi)`).
- `src/ngscopeclient/CMakeLists.txt`: forces `windres` as the RC compiler, and only handles GNU/Clang when setting
  the Win32 subsystem (MSVC needs `/SUBSYSTEM:WINDOWS` and `/ENTRY:mainCRTStartup`, or a `WinMain`).
- OpenMP: `find_package(OpenMP)` gives MSVC's `/openmp` (OpenMP 2.0), which rejects unsigned loop indices (most
  `parallel for` loops use `size_t`) and has no `omp task` (`lib/scopeprotocols/TimeOutsideLevelMeasurement.cpp`).
  Use `/openmp:llvm` and ship `libomp140.x86_64.dll`; check task support, or rewrite that one filter.
- Shared libraries: `scopehal`, `scopeprotocols`, `xptools` and `log` are `SHARED`. MinGW exports everything
  automatically; MSVC does not. `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` covers functions but not data (about 45 `extern`
  globals such as `g_hasAvx2`, plus static class members such as the driver and filter factory tables). Either add
  export/import macros, or build these libraries `STATIC` on MSVC.
- Packaging: the portable zip/MSI target in `src/ngscopeclient/CMakeLists.txt` uses MSYS2 tools (`where gcc`,
  `pacman`, `mingw-bundledlls`). vcpkg copies dependency DLLs next to the executable; consider not shipping its
  `vulkan-1.dll`, since the loader normally comes with the GPU driver.

Sources (mechanical):

- `__attribute__((noinline))`, `((packed))` (`lib/scopehal/LevelCrossingDetector.h`) and
  `((target("avx2")))` etc.: replace with portable macros (MSVC needs no target attribute for intrinsics).
- `__builtin_cpu_supports`, `__builtin_clzll`, `__builtin_bswap32` (`lib/scopehal/scopehal.cpp`) and
  `__builtin_assume_aligned` (some filters): use `__cpuid`/`__cpuidex`, `_BitScanReverse64`, `_byteswap_ulong`.
- `ssize_t` needs a typedef on MSVC. libiio's `iio.h` does `typedef ptrdiff_t ssize_t` unless `_SSIZE_T_DEFINED`
  is set, so define that macro alongside our typedef to avoid a conflicting redefinition.
- POSIX headers (`unistd.h`, `sys/*`, `dlfcn.h`, `dirent.h`) that are not already under `_WIN32` guards.
- `lib/log/log.h`: `ATTR_FORMAT`/`ATTR_NORETURN` are empty on MSVC (marked FIXME); `[[noreturn]]` works there.

An alternative to plain `cl.exe` is `clang-cl`: same MSVC ABI and the same vcpkg dependencies, but it accepts the
existing `__attribute__`/`__builtin_*` code, which removes most of the source changes above.
