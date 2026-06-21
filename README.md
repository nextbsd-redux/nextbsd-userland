# nextbsd-userland

Cross-built **Apple system layer** for NextBSD — the Mach / launchd / configd /
CoreFoundation / IOKit stack and its daemons — built on an Ubuntu host with the
`nextbsd-kernel-toolchain` clang cross-compiler, exactly like `nextbsd-kernel`
and `nextbsd-freebsd-compat`. Publishes a rolling `continuous` userland artifact
that the `nextbsd` ISO assembler ingests.

> **Status: CI workflow scaffold.** This commit is the workflow harness only —
> container + cross sysroot + buildenv setup, no build/publish. The vendored
> Apple sources (Tier 0–2), the `build-userland.sh` driver, the cmake cross
> toolchains, the build/pack/publish steps, and the license all land in a
> follow-up **code-drop PR**, reviewed in one pass.

## Scope

**In (Tiers 0–2 — the system layer that has no FreeBSD equivalent):**

| Tier | Components |
|------|-----------|
| 0 — host tool | `migcom` + `mig` (arch-neutral MIG codegen, built for the runner) |
| 1 — foundation libs | `libmach`→`libsystem_kernel` → `libdispatch` *(cmake)* → `libxpc` → `liblaunch` → `swift-foundation-icu` *(cmake)* → `libCoreFoundation` |
| 2 — daemons/services | `launchd`+`launchctl` → `configd` → `libSystemConfiguration` → `libIOKit`+`ioreg` → `kext_tools` → `Libnotify`+`notifyd` → syslog/asl stack → `IPConfiguration` → `mDNSResponder` → `DiskArbitration` → `hostnamed` |

**Out (by design):**

- **POSIX command suites** (`file_cmds`, `shell_cmds`, `text_cmds`, `adv_cmds`,
  `system_cmds`) — for generic POSIX tools Apple's build buys nothing over
  FreeBSD's, so these come from **FreeBSD source curated in
  `nextbsd-freebsd-compat`'s `srclist.txt`**, synced to the releng train.
- **PAM** (OpenPAM + pam_modules) — a later pass.

## Where it fits in the build chain

```
toolchain → nextbsd-freebsd-compat (base) ─┐
                                           ├→ nextbsd-userland → nextbsd (ISO)
            nextbsd-kernel → modules ──────┘
```

- **Sysroot:** staged from compat's `continuous` base tarball (hard dependency —
  userland links the from-source libc/libsys/headers).
- **Trigger:** `repository_dispatch: base-updated` from compat; `workflow_dispatch`.
- **Publishes:** `continuous` (`nextbsd-userland-<arch>.tar.gz`), then dispatches
  `userland-updated` → `nextbsd`.
- **Arches:** `amd64` + `arm64`, both cross-built on the x86_64 runner.

## Build model

`bsd.lib.mk`/`bsd.prog.mk` components cross-compile under `make.py … buildenv`
with the toolchain clang (zero source changes). The two CMake holdouts
(`libdispatch`, `swift-foundation-icu`) build on the runner with a cross
toolchain file (`cmake/cross-<arch>.cmake`). `migcom` builds for the runner as a
host tool. Driver order and the full design are in the
[repo-split plan](https://pkgdemon.github.io/nextbsd-userland-repo-plan.html).
