# cross-arm64.cmake — CMake cross toolchain for nextbsd-userland's two
# CMake components (libdispatch, swift-foundation-icu) when TARGET=arm64.
#
# Same shape as cross-amd64.cmake; only the triple + SYSTEM_PROCESSOR
# differ. See cross-amd64.cmake's header for the full rationale.
#
# Triple: aarch64-unknown-freebsd (matches make.py's arm64 target arch).

set(CMAKE_SYSTEM_NAME      FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(_triple  "aarch64-unknown-freebsd")
set(_sysroot "$ENV{SYSROOT}")
set(_bindir  "$ENV{CROSS_BINDIR}")

set(CMAKE_C_COMPILER   "${_bindir}/clang")
set(CMAKE_CXX_COMPILER "${_bindir}/clang++")

set(CMAKE_C_FLAGS_INIT   "--target=${_triple} --sysroot=${_sysroot}")
set(CMAKE_CXX_FLAGS_INIT "--target=${_triple} --sysroot=${_sysroot}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "--target=${_triple} --sysroot=${_sysroot}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "--target=${_triple} --sysroot=${_sysroot}")

set(CMAKE_SYSROOT        "${_sysroot}")
set(CMAKE_FIND_ROOT_PATH "${_sysroot}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
