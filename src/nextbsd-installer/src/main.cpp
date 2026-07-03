// nextbsd-installer — FTXUI front-end entry point + screen router.
#include <ftxui/component/screen_interactive.hpp>

#include <cstdio>
#include <cstring>
#include <string>

#ifdef __FreeBSD__
#include <sys/reboot.h>   // reboot(2), RB_AUTOBOOT / RB_POWEROFF
#include <unistd.h>       // sync(2), execl(2)
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
      case Screen::Shell:
#ifdef __FreeBSD__
        // Drop to a real interactive shell, replacing the installer image.
        // FTXUI restored the terminal when the Loop exited, so /bin/sh gets a
        // usable console. If exec fails, fall through to a clean exit (which
        // returns to whatever launched the installer).
        if (!st.demo && !st.dry_run) {
          // Clear the screen so the shell starts clean. No clear/tput binary
          // needed — the console interprets these bare ANSI escapes itself:
          // show cursor, reset attrs, erase screen + scrollback, home cursor.
          std::fputs("\033[?25h\033[0m\033[2J\033[3J\033[H", stdout);
          std::fflush(nullptr);
          execl("/bin/sh", "sh", "-i", static_cast<char*>(nullptr));
        }
#endif
        return 0;
      case Screen::Quit:
      default:              return 0;
    }
  }
}
