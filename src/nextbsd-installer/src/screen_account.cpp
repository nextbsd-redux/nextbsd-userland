// Phase 3 — Admin account + hostname. The hostname is auto-suggested from the
// username + DMI model and stays editable (a dirty flag stops clobbering it).
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <string>

#include "engine.hpp"
#include "screens.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace nbi {

Screen run_account(ScreenInteractive& screen, AppState& st) {
  Screen next = Screen::Disk;
  if (st.hostname.empty())
    st.hostname = engine::suggest_hostname(st.username, st.dmi_model);

  InputOption uo = InputOption::Default();
  uo.multiline = false;
  uo.on_change = [&] {
    if (!st.hostname_dirty)
      st.hostname = engine::suggest_hostname(st.username, st.dmi_model);
  };
  auto user = Input(&st.username, "username", uo);

  InputOption po = InputOption::Default();
  po.multiline = false;
  po.password = true;
  auto pass = Input(&st.password, "password", po);
  auto conf = Input(&st.confirm, "confirm", po);

  InputOption ho = InputOption::Default();
  ho.multiline = false;
  ho.on_change = [&] { st.hostname_dirty = true; };
  auto host = Input(&st.hostname, "hostname", ho);

  auto next_btn = Button("Next", [&] {
    if (!st.username.empty() && !st.password.empty() && st.password == st.confirm) {
      next = Screen::Install;
      screen.Exit();
    }
  });
  auto back_btn = Button("Back", [&] { next = Screen::Disk; screen.Exit(); });

  auto layout = Container::Vertical({
      user, pass, conf, host,
      Container::Horizontal({next_btn, back_btn}),
  });

  auto renderer = Renderer(layout, [&] {
    bool match = !st.password.empty() && st.password == st.confirm;
    Element match_el =
        st.confirm.empty()
            ? text("")
            : (match ? text("✓ match") | color(theme::ok)
                     : text("✗ does not match") | color(theme::bad));

    auto field = [](const std::string& label, Element in) {
      return hbox({text(label) | color(theme::amberBright) | size(WIDTH, EQUAL, 12),
                   std::move(in) | flex | border});
    };

    auto body = vbox({
        theme::hint("This user is added to the wheel group and may use sudo."),
        text(""),
        field("Username", user->Render()),
        field("Password", pass->Render()),
        hbox({field("Confirm", conf->Render()) | flex, text(" "), match_el}),
        text(""),
        field("Hostname", host->Render()),
        theme::hint("  suggested from username + DMI model — editable"),
        st.dmi_model.empty() ? text("")
                             : theme::hint("  DMI: " + st.dmi_model + "  →  slug: " +
                                           engine::slugify(st.dmi_model)),
        filler(),
        hbox({next_btn->Render(), text("  "), back_btn->Render()}),
    });
    return theme::chrome("Create Admin Account", st.build_id, body,
                         {{"Tab", "field"}, {"Enter", "confirm"}});
  });

  screen.Loop(renderer);
  return next;
}

} // namespace nbi
