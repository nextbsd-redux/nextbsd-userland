# nextbsd-userland

Cross-built **Darwin system layer** for NextBSD — the Mach / launchd / configd /
CoreFoundation / IOKit stack and its daemons — built on an Ubuntu host with the
`nextbsd-kernel-toolchain` clang cross-compiler, exactly like `nextbsd-kernel`
and `nextbsd-freebsd-compat`. Publishes a rolling `continuous` userland artifact
that the `nextbsd` ISO assembler ingests.

> **Status: CI workflow scaffold.** This commit is the workflow harness only —
> container + cross sysroot + buildenv setup, no build/publish. The vendored
> Darwin sources (Tier 0–2), the `build-userland.sh` driver, the cmake cross
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

## System path layout — the four-domain model

NextBSD uses an explicit
[**four-domain filesystem layout**](https://pkgdemon.github.io/nextbsd-path-domain-conformance-plan.html) —
**not** Apple's convention where the bare `/Library` *is* the Local domain. Every
system resource lives under an explicit domain root rather than a bare `/Library`,
keeping `/System` reserved for the System domain and the OS's admin-installed
resources in the Local domain:

| Domain  | Root            | Owner / meaning                    |
|---------|-----------------|------------------------------------|
| System  | `/System/Library` | OS-provided, immutable            |
| Local   | `/Local/Library`  | this machine, admin-installed     |
| Network | `/Network/Library`| network-shared (reserved)         |
| User    | `~/Library`       | per-user                          |

Consequently the Darwin-source components in this repo use `/Local/Library` for
everything Apple would have put in the bare `/Library` (Local) domain. The
canonical locations these components read/write:

| Path | Component | Purpose |
|------|-----------|---------|
| `/System/Library/LaunchDaemons` | launchd (PID 1) | OS daemon plists (scanned first) |
| `/Local/Library/LaunchDaemons`  | launchd (PID 1) | third-party daemon plists (scanned second) |
| `/Local/Library/StartupItems`   | launchctl | legacy StartupItems bootstrap dir |
| `/Local/Library/Preferences/SystemConfiguration` | `libSystemConfiguration` (`PREFS_DEFAULT_DIR`) | SCPreferences store — `preferences.plist` (`ComputerName`, network config), `com.apple.Boot.plist` |
| `/Local/Library/Preferences`    | `libCoreFoundation` | CFPreferences "any user" / computer domain |
| `~/Library/Preferences`         | `libCoreFoundation` | CFPreferences current-user (User domain — **not** `/Local`) |
| `/System/Library/Extensions`    | `kext_tools` | OS kext bundles |
| `/Local/Library/Extensions`     | `kext_tools` | auxiliary (admin-installed) kext bundles |
| `/Local/Library/Logs`, `/Local/Library/Logs/CrashReporter` | syslog/ASL | log + crash-report directories |

Rule of thumb for new code: a shared/computer-wide resource goes under
`/Local/Library`; an OS-shipped one under `/System/Library`; a per-user one under
`~/Library`. Never introduce a bare `/Library/...` path.
