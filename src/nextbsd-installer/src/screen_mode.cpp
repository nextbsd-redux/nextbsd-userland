// Phase 1 — Install Mode. Fresh install onto a selected disk, plus a live-shell
// escape and Reboot. (Upgrade removed.)
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
      "Shell      Drop to a live rescue shell",
  };
  int selected = 0;
  auto menu = Menu(&entries, &selected);

  auto activate = [&] {
    switch (selected) {
      case 0: next = Screen::Disk;   screen.Exit(); break;
      case 1: next = Screen::Shell;  screen.Exit(); break;
    }
  };

  auto comp = CatchEvent(menu, [&](Event e) {
    if (e == Event::Return) { activate(); return true; }
    return false;
  });

  auto renderer = Renderer(comp, [&] {
    std::string scanned = st.disks.empty()
                              ? std::string("no disks")
                              : (std::to_string(st.disks.size()) + " disk(s)");
    Element probe = theme::hint("Probe: scanned " + scanned + ".");
    auto body = vbox({
        text("Welcome to NextBSD.") | color(theme::amberBright),
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
