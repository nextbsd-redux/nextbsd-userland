// Phase 2 — Target disk selection (whole-disk only). Disks are shown with their
// volume labels + fs types; the booted install medium is hidden from the list.
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

  // Only installable disks are listed — the booted install medium (is_media) is
  // hidden entirely, not shown locked. shown[row] maps a menu row back to its
  // index in st.disks.
  std::vector<int> shown;
  std::vector<std::string> entries;
  for (int i = 0; i < static_cast<int>(st.disks.size()); ++i) {
    if (st.disks[i].is_media) continue;
    const auto& d = st.disks[i];
    entries.push_back(d.dev + "   " + d.size + "   " + d.model);
    shown.push_back(i);
  }
  int selected = 0;
  auto menu = Menu(&entries, &selected);

  auto choose = [&] {
    if (shown.empty()) return;
    st.disk_index = shown[selected];
    next = Screen::Install;
    screen.Exit();
  };

  auto comp = CatchEvent(menu, [&](Event e) {
    if (e == Event::Return) { choose(); return true; }
    if (e == Event::Escape) { next = Screen::Mode; screen.Exit(); return true; }
    return false;
  });

  auto renderer = Renderer(comp, [&] {
    Elements vols;
    if (!shown.empty()) {
      const auto& d = st.disks[shown[selected]];
      for (const auto& v : d.volumes)
        vols.push_back(theme::hint("     " + v.label + "   " + v.size + "   " +
                                   v.fstype + (v.note.empty() ? "" : "  " + v.note)));
    }
    Element foot =
        shown.empty()
            ? theme::hint("No installable disk found (the boot medium is hidden).")
            : theme::hint("Enter erases the disk and installs NextBSD onto it.");
    auto body = vbox({
        text("The selected disk will be ERASED in full.") | color(theme::amberBright),
        text(""),
        menu->Render(),
        text(""),
        vbox(std::move(vols)),
        filler(),
        foot,
    });
    return theme::chrome("Select Install Disk", st.build_id, body,
                         {{"↑↓", "move"}, {"Enter", "select"}, {"Esc", "back"}});
  });

  screen.Loop(renderer);
  return next;
}

} // namespace nbi
