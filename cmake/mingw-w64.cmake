# Cross-build EDF6Coop.dll and its tests from Linux: clang for the x86_64-w64-mingw32 target, MinGW-w64's headers,
# runtime and libstdc++, lld; ctest runs the tests under Wine.
#   cmake -S . -B build-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
# Fedora: dnf install clang lld mingw64-gcc-c++ mingw64-winpthreads-static wine
# (GCC 16 for this target stops with an internal compiler error on tests/gamenet; clang builds everything.)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TARGET x86_64-w64-mingw32)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${MINGW_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${MINGW_TARGET})
set(CMAKE_RC_COMPILER ${MINGW_TARGET}-windres)
set(CMAKE_EXE_LINKER_FLAGS_INIT -fuse-ld=lld)
set(CMAKE_SHARED_LINKER_FLAGS_INIT -fuse-ld=lld)
set(CMAKE_MODULE_LINKER_FLAGS_INIT -fuse-ld=lld)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TARGET}/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

find_program(WINE wine REQUIRED)
set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE})
