# kext.subproj — vendored Apple OSKext (faithful port, #182)

Apple's `OSKext` engine + helpers, vendored from:

- **`IOKitUser-907.100.13`** (OS X **10.9** — deliberately **pre-SIP**, so no
  codesign enforcement, no kernelcache/AuxKC collection machinery to strip)
- Source: `github.com/apple-oss-distributions/IOKitUser` @ tag
  `IOKitUser-907.100.13`, path `kext.subproj/`
- License: **APSL 2.0** (headers intact)

This is the `OSKext` bundle/dependency/load engine that the faithful
`kext_tools` port (#182) is built on — bundle discovery, Info.plist parsing,
`OSBundleLibraries` dependency-graph resolution, version/compat, validation.

## Status: VENDORED AND BUILT

These files are **not** wired into the build yet — they sit here verbatim as
the starting point. The phased port (see the plan) is:

1. **Phase 0** — vendor (this commit) → a fast standalone `libkext` compile
   (compile-only CI job, sysroot from the `continuous` image) so we can
   iterate the 15.7k-LOC `OSKext.c` against NextBSD's `libCoreFoundation` /
   `libIOKit` in minutes, not full ISO builds.
2. **Re-back XNU → kld** — `OSKextLoad`/unload/query swap from the
   `kext_request` Mach trap to `kldload`/`kldstat`/`kldunload`; personalities
   from `IOCatalogueSendData` to the libIOKit/hwregd matcher; KXLD/mkext/
   prelink deleted (the kernel's `kld` links). Codesign stubbed (no SIP).
3. **Phase 1** — dependency-graph resolution (first user-visible win) feeding
   the bulk conversion (#179) and kextd (#177).

Plan: https://pkgdemon.github.io/nextbsd-oskext-port-plan.html

The minimal kld-backed `kextload`/`kextstat`/`kextunload` trio in the parent
directory remains the shipping tooling until this port graduates.

## Corrections (nextbsd-userland#154)

- **The OS era above was wrong.** `IOKitUser-907.100.13` is **OS X 10.9**, not
  10.8.5. Apple's own `distribution-macOS` pins `rel/macOS-10.8` to
  `IOKitUser-755.24.1` and `rel/macOS-10.9` to `IOKitUser-907.100.13`.
  The pre-SIP rationale is unaffected (SIP arrived in 10.11) — and 10.9 happens
  to be the **last pre-XPC IOKitUser**, the first with XPC being
  `IOKitUser-1050.1.21` (10.10), so the choice is better than this file claimed.

- **No longer verbatim.** 15 of 18 files are byte-identical to the tag; three
  carry NextBSD edits: `OSKext.c` (kld backing, `kNilOptions` shim, safe-boot
  stub), `OSKextPrivate.h` and `bootfiles.h` (`/Library` -> `/Local/Library`).

- **It is built.** `kext_tools/Makefile` lists `kext.subproj` in `SUBDIR`, and
  `libkext.a` is on the `LDADD` line of all four CLIs plus `kextdeps`.
