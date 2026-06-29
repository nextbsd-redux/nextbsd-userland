// Phase 1 — Install Mode. Upgrade is offered only when a probe found an
// existing NextBSD root; otherwise it's shown dimmed and does nothing.
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <string>
#include <vector>

#include "engine.hpp"
#include "screens.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace nbi {

Screen run_mode(ScreenInteractive& screen, AppState& st) {
  Screen next = Screen::Quit;

  std::vector<std::string> entries = {
      "Install    Fresh install onto a selected disk",
      st.existing.found
          ? ("Upgrade    Keep data, replace base — " + st.existing.dev)
          : "Upgrade    (no existing NextBSD install detected)",
      "Shell      Drop to a live rescue shell",
      "Reboot     Restart the machine",
  };
  int selected = 0;
  auto menu = Menu(&entries, &selected);

  auto activate = [&] {
    switch (selected) {
      case 0: next = Screen::Disk; screen.Exit(); break;
      case 1:
        if (st.existing.found) { st.mode = Mode::Upgrade; next = Screen::Disk; screen.Exit(); }
        break;  // dimmed/disabled when no install found
      case 2: next = Screen::Shell; screen.Exit(); break;
      case 3: next = Screen::Reboot; screen.Exit(); break;
    }
  };

  auto comp = CatchEvent(menu, [&](Event e) {
    if (e == Event::Return) { activate(); return true; }
    return false;
  });

  auto renderer = Renderer(comp, [&] {
    Element probe =
        st.existing.found
            ? (text("Probe: " + st.existing.dev + "  " + st.existing.version + "  (upgradable)") |
               color(theme::ok))
            : theme::hint("Probe: scanned " +
                          (st.disks.empty() ? std::string("(no disks)") : st.disks.front().dev) +
                          " — found no bootable NextBSD root.");
    auto body = vbox({
        text("Welcome to NextBSD Server.") | color(theme::amberBright),
        text(""),
        text("Choose an action:"),
        text(""),
        menu->Render(),
        filler(),
        probe,
    });
    return theme::chrome("Install Mode", st.build_id, body,
                         {{"↑↓", "move"}, {"Enter", "select"}});
  });

  screen.Loop(renderer);
  return next;
}

} // namespace nbi
