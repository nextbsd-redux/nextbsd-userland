# Cross-build notes (Tier 0-2)

Grounded design for `build-userland.sh`, derived from FreeBSD `Makefile.inc1`
(releng/15.1), the original native `nextbsd-work/build.sh` recipes, and the
component sources. This is the "why" behind the driver.

## Sysroot model (the foundational wiring)

The buildenv compiler is invoked with `--sysroot=${WORLDTMP}` and **no host
include/lib fallback** (`Makefile.inc1` XCFLAGS, `-B${WORLDTMP}/usr/bin`,
`--sysroot=${WORLDTMP}`). We **reuse the image's baked `kernel-toolchain`**,
which deliberately **skips `_includes` and `_libraries`**
(`KERNEL_TOOLCHAIN_TGTS = TOOLCHAIN_TGTS:N_obj:N_cleanobj:N_includes:N_libraries`),
so `${WORLDTMP}` has the cross-tools but **no base headers/libs**.

Fix: stage the compat-`continuous` base (libc/libsys/headers, built from source)
**into `${WORLDTMP}`** (headers+libs only — not `usr/bin`/`bin`/`sbin`, which
would clobber the cross-tools), and **collapse `SYSROOT → ${WORLDTMP}`**. Then:

- compiler `--sysroot=${WORLDTMP}` finds base `sys/types.h`, `net/*`, libc;
- Makefiles' `-I$SYSROOT/usr/include` + `-L$SYSROOT/usr/lib/system` resolve in
  the same root;
- `DESTDIR=/stage` stays separate → clean artifact;
- `sync_sysroot` mirrors each built lib `DESTDIR → SYSROOT(=WORLDTMP)` so the
  next component links/includes the previous tier.

## migcom (Tier 0) — host code generator

migcom turns `.defs` into C and must run **on the build host**. Built the
FreeBSD way (same as bootstrapping yacc/lex): staged into `/usr/src/libexec/migcom`
and built host-native via the **`_legacy` stage** with `LOCAL_LEGACY_DIRS`
(`make obj includes all install` under BMAKE = host gcc). It is **build-only —
NOT shipped** (a runner-native x86_64-Linux binary would be broken on the FreeBSD
ISO). `MIGCOM` points at its `${WORLDTMP}/legacy` location for the MIG steps.
The legacy host compiler is GCC, which lacks Apple's `__private_extern__`; the
Makefile maps it away (`-D__private_extern__=`).

## MIG codegen — out of band

Base `bsd.*.mk` has **no MIG support**. All `.defs` are processed by the host
migcom via `run_mig` into a per-subsystem `$MIGOUT` dir, consumed by the
component's Makefile via `.PATH: ${MIGOUT}` + `-I${MIGOUT}`. `mig.sh` runs
`$MIGCC -E` (host cpp) piped to `$MIGCOM` — fully host-native, arch-neutral.

## Build order (cross)

1. libmach (+ mach/* headers, servers/bootstrap.h)
2. libdispatch (CMake; +BlocksRuntime, `.so.0`/`libsystem_*` symlinks)
3. install `mach/mach.h` umbrella + uuid/Availability/launch shims
4. libxpc
5. launchd MIG stubs (`$MIG_OUT`) → liblaunch
6. swift-foundation-icu (CMake) + `lib_FoundationICU.so`/`libicucore` symlinks
7. libCoreFoundation
8. launchd → launchctl
9. configd MIG → configd → libSystemConfiguration
10. libIOKit → ioreg → kext_tools (+ sys/iocatalogue.h)
11. Libnotify MIG → libnotify → ASL MIG → libsystem_asl → notifyd → syslogd → aslmanager → syslog(1)
12. IPConfiguration MIG → ipconfigd + ipconfig CLI
13. mDNSResponder → libdns_sd
14. DiskArbitration
15. hostnamed (last — needs libSC + libnotify + libdns_sd)

## Known blockers / risks

1. **swift-foundation-icu `freebsd_decode_icu_chunks`** (the one structural
   blocker): built with the cross toolchain, then *executed on the x86 host* to
   decode the ICU data blob → "Exec format error". Must be built **host-native**
   (or the decoded `.bin` pre-generated). Distinct, contained CMake change.
2. **Sysroot triangulation** — see above; the collapse-to-`WORLDTMP` fix.
3. **ICU/CF define triad** (`__HAS_APPLE_ICU__`, `U_DISABLE_RENAMING=1`,
   `APPLE_ICU_CHANGES=1`) must be identical across ICU/CF/launchd/launchctl/
   configd/libSC, and `-l_FoundationICU` needs the `lib_FoundationICU.so` dev
   symlink, or rtld fails on renamed symbols.
4. **Base network headers** for IPConfiguration/mDNS (`net/if.h`, `net/if_dl.h`,
   `net/route.h`, `netinet/in_var.h`, `getifaddrs`) must be in the sysroot;
   Apple-vs-FreeBSD `struct ifreq`/`if_data` deltas are the likely error surface.
5. **INCSDIR no-auto-mkdir** (`bsd.incs.mk`) — every header-installing lib needs
   its INCSDIR pre-created; libdispatch's CMake `mig -I/usr/include` needs sysroot
   redirection.

Everything else (libxpc, liblaunch, CF, launchd, launchctl, configd, libSC,
libIOKit, kext_tools, notify/asl, IPConfiguration, mDNS, libdns_sd,
DiskArbitration, hostnamed) is cross-clean by construction: `$(CC)`-driven
`bsd.mk`, uniform `SYSROOT`/`MIGOUT` guards, host-native MIG, no build-time
TARGET execution. Residual risk is sysroot population + link order.
