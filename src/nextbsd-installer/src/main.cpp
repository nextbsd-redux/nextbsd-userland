// nextbsd-installer — FTXUI front-end entry point + screen router.
#include <ftxui/component/screen_interactive.hpp>

#include <cstdio>
#include <cstring>
#include <string>

#ifdef __FreeBSD__
#include <sys/reboot.h>   // reboot(2), RB_AUTOBOOT / RB_POWEROFF
#include <unistd.h>       // sync(2)
#endif

#include "app.hpp"
#include "engine.hpp"
#include "screens.hpp"

using namespace nbi;

int main(int argc, char** argv) {
  AppState st;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--demo")) {
      st.demo = true;
      st.dry_run = true;
    } else if (!std::strcmp(argv[i], "--dry-run")) {
      st.dry_run = true;
    } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
      std::printf(
          "usage: nextbsd-installer [--demo] [--dry-run]\n"
          "  --demo     synthetic disks + simulated install (no hardware)\n"
          "  --dry-run  engine prints actions instead of executing them\n");
      return 0;
    }
  }
#ifndef __FreeBSD__
  st.demo = true;     // off-target host: only the demo path makes sense
  st.dry_run = true;
#endif

  engine::probe(st);

  auto screen = ftxui::ScreenInteractive::Fullscreen();
  for (Screen cur = Screen::Mode;;) {
    switch (cur) {
      case Screen::Mode:    cur = run_mode(screen, st); break;
      case Screen::Disk:    cur = run_disk(screen, st); break;
      case Screen::Account: cur = run_account(screen, st); break;
      case Screen::Install: cur = run_install(screen, st); break;
      case Screen::Finish:  cur = run_finish(screen, st); break;
      case Screen::Reboot:
#ifdef __FreeBSD__
        if (!st.demo && !st.dry_run) { std::fflush(nullptr); sync(); reboot(RB_AUTOBOOT); }
#endif
        return 0;   // only reached in demo/dry-run/off-target (reboot() doesn't return)
      case Screen::Shutdown:
#ifdef __FreeBSD__
        if (!st.demo && !st.dry_run) { std::fflush(nullptr); sync(); reboot(RB_POWEROFF); }
#endif
        return 0;
      case Screen::Shell:   // exit cleanly back to the shell that launched us
      case Screen::Quit:
      default:              return 0;
    }
  }
}
