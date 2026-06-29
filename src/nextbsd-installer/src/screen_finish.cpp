// Phase 5 — Finish. Success summary + the two terminal actions (Reboot /
// Shutdown) plus a Shell escape hatch. On failure, shows the failing stage.
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <string>

#include "screens.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace nbi {

Screen run_finish(ScreenInteractive& screen, AppState& st) {
  Screen next = Screen::Reboot;

  auto reboot = Button("Reboot", [&] { next = Screen::Reboot; screen.Exit(); });
  auto shut = Button("Shut Down", [&] { next = Screen::Shutdown; screen.Exit(); });
  auto sh = Button("Shell", [&] { next = Screen::Shell; screen.Exit(); });
  auto layout = Container::Horizontal({reboot, shut, sh});

  const std::string disk = st.disks.empty() ? "?" : st.disks[st.disk_index].dev;

  auto renderer = Renderer(layout, [&] {
    Element head =
        st.success
            ? (text("✓  NextBSD Server installed successfully.") | color(theme::ok) | bold)
            : (text("✗  Installation failed: " + st.fail_stage) | color(theme::bad) | bold);
    auto row = [](const std::string& k, const std::string& v) {
      return hbox({text("  " + k) | color(theme::amberDim) | size(WIDTH, EQUAL, 14),
                   text(v) | color(theme::amberBright)});
    };
    auto body = vbox({
        head,
        text(""),
        row("Build", st.build_id),
        row("Disk", disk),
        row("Hostname", st.hostname),
        row("Admin user", st.username + "  (wheel, sudo)"),
        text(""),
        theme::hint("Remove the install medium before rebooting."),
        filler(),
        hbox({reboot->Render(), text("  "), shut->Render(), text("  "), sh->Render()}),
    });
    return theme::chrome("Installation Complete", st.build_id, body,
                         {{"Tab", "move"}, {"Enter", "select"}});
  });

  screen.Loop(renderer);
  return next;
}

} // namespace nbi
