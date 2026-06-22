# cross-amd64.cmake — CMake cross toolchain for nextbsd-userland's two
# CMake components (libdispatch, swift-foundation-icu) when TARGET=amd64.
#
# These are the only CMake builds in the system layer. cmake + ninja run
# ON THE HOST (the x86_64 Ubuntu runner), but emit FreeBSD/amd64 objects
# by driving the cross clang from $CROSS_BINDIR against the staged
# $SYSROOT. Everything else (bsd.lib.mk / bsd.prog.mk components) goes
# through `make.py ... buildenv`, not this file.
#
# Required env (exported by build-userland.sh before invoking cmake):
#   CROSS_BINDIR — dir holding the cross clang/clang++ (e.g. the freebsd
#                  toolchain's bin); we use the in-tree clang there.
#   SYSROOT      — the staged sysroot (compat continuous base + the
#                  headers `make.py ... toolchain _includes` populated +
#                  whatever earlier tiers installed into /stage that ICU/
#                  dispatch need at include/link time).
#
# Triple: x86_64-unknown-freebsd (matches make.py's amd64 target arch).

set(CMAKE_SYSTEM_NAME      FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# $ENV{CROSS_BINDIR} / $ENV{SYSROOT} are read from the environment the
# driver script exports, so one toolchain file works regardless of the
# absolute runner path.
set(_triple  "x86_64-unknown-freebsd")
set(_sysroot "$ENV{SYSROOT}")
set(_bindir  "$ENV{CROSS_BINDIR}")

set(CMAKE_C_COMPILER   "${_bindir}/clang")
set(CMAKE_CXX_COMPILER "${_bindir}/clang++")

# --target + --sysroot make the host clang emit FreeBSD/amd64 code and
# resolve headers/libs out of the staged sysroot, not the runner's /usr.
set(CMAKE_C_FLAGS_INIT   "--target=${_triple} --sysroot=${_sysroot}")
set(CMAKE_CXX_FLAGS_INIT "--target=${_triple} --sysroot=${_sysroot}")
# Carry the target/sysroot into link lines too (clang drives lld).
set(CMAKE_EXE_LINKER_FLAGS_INIT    "--target=${_triple} --sysroot=${_sysroot}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "--target=${_triple} --sysroot=${_sysroot}")

set(CMAKE_SYSROOT        "${_sysroot}")
set(CMAKE_FIND_ROOT_PATH "${_sysroot}")

# Find PROGRAMS on the host (cmake/ninja/migcom helpers), but LIBRARIES +
# HEADERS only under the sysroot — never pick up the runner's amd64-Linux
# libs/headers.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
