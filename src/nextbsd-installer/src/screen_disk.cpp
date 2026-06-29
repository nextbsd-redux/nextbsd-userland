// Phase 2 — Target disk selection (whole-disk only). Disks are shown with their
// volume labels + fs types; the live install medium is shown but locked.
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <string>
#include <vector>

#include "screens.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace nbi {

Screen run_disk(ScreenInteractive& screen, AppState& st) {
  Screen next = Screen::Mode;

  std::vector<std::string> entries;
  for (const auto& d : st.disks) {
    std::string s = d.dev + "   " + d.size + "   " + d.model;
    if (d.is_media) s += "   (install media — locked)";
    entries.push_back(s);
  }
  int selected = 0;
  auto menu = Menu(&entries, &selected);

  auto choose = [&] {
    if (st.disks.empty()) return;
    if (st.disks[selected].is_media) return;  // can't install onto the medium
    st.disk_index = selected;
    next = Screen::Account;
    screen.Exit();
  };

  auto comp = CatchEvent(menu, [&](Event e) {
    if (e == Event::Return) { choose(); return true; }
    if (e == Event::Escape) { next = Screen::Mode; screen.Exit(); return true; }
    return false;
  });

  auto renderer = Renderer(comp, [&] {
    Elements vols;
    if (!st.disks.empty()) {
      const auto& d = st.disks[selected];
      for (const auto& v : d.volumes)
        vols.push_back(theme::hint("     " + v.label + "   " + v.size + "   " +
                                   v.fstype + (v.note.empty() ? "" : "  " + v.note)));
    }
    bool media = !st.disks.empty() && st.disks[selected].is_media;
    auto body = vbox({
        text("The selected disk will be ERASED in full.") | color(theme::amberBright),
        text(""),
        menu->Render(),
        text(""),
        vbox(std::move(vols)),
        filler(),
        media ? theme::hint("This is the live install medium — it cannot be erased.")
              : theme::hint("Enter erases the disk and installs NextBSD onto it."),
    });
    return theme::chrome("Select Install Disk", st.build_id, body,
                         {{"↑↓", "move"}, {"Enter", "select"}, {"Esc", "back"}});
  });

  screen.Loop(renderer);
  return next;
}

} // namespace nbi
