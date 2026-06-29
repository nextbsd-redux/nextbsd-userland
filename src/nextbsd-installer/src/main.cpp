// nextbsd-installer — FTXUI front-end entry point + screen router.
#include <ftxui/component/screen_interactive.hpp>

#include <cstdio>
#include <cstring>
#include <string>

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
      case Screen::Reboot:  std::printf("[would reboot]\n"); return 0;
      case Screen::Shutdown:std::printf("[would shut down]\n"); return 0;
      case Screen::Shell:   std::printf("[would drop to shell]\n"); return 0;
      case Screen::Quit:
      default:              return 0;
    }
  }
}
