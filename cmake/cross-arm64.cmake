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
# Force clang to use lld (multi-target). The host /usr/bin/ld is x86-only and
# can't link aarch64 objects ("crt1.o: file in wrong format"); the bsd.mk builds
# use ld.lld for the same reason. Applies to the compiler check + every link.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "--target=${_triple} --sysroot=${_sysroot} --ld-path=${_bindir}/ld.lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "--target=${_triple} --sysroot=${_sysroot} --ld-path=${_bindir}/ld.lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "--target=${_triple} --sysroot=${_sysroot} --ld-path=${_bindir}/ld.lld")

set(CMAKE_SYSROOT        "${_sysroot}")
set(CMAKE_FIND_ROOT_PATH "${_sysroot}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
