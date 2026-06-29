// Phase 4 — Install progress. A worker thread drives the engine and reports
// progress; the UI thread redraws on each posted Custom event and exits when done.
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "engine.hpp"
#include "screens.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace nbi {

Screen run_install(ScreenInteractive& screen, AppState& st) {
  std::atomic<int> pct{0};
  std::atomic<bool> done{false};
  std::mutex m;
  std::string status = "Preparing…";

  std::thread worker([&] {
    engine::run_install(st, [&](int p, const std::string& s) {
      pct.store(p);
      { std::lock_guard<std::mutex> lk(m); status = s; }
      screen.PostEvent(Event::Custom);
    });
    done.store(true);
    screen.PostEvent(Event::Custom);
  });

  auto view = Renderer([&] {
    int p = pct.load();
    std::string s;
    { std::lock_guard<std::mutex> lk(m); s = status; }
    auto body = vbox({
        theme::title(s),
        text(""),
        hbox({gauge(p / 100.0f) | color(theme::amberBright) | flex,
              text("  " + std::to_string(p) + "%") | color(theme::amberBright) | bold}),
        filler(),
    });
    return theme::chrome("Installing NextBSD", st.build_id, body,
                         {{"", "cloning the base with cpdup…"}});
  });

  // Exit the loop once the worker signals completion.
  auto comp = CatchEvent(view, [&](Event) {
    if (done.load()) { screen.Exit(); return true; }
    return false;
  });

  screen.Loop(comp);
  if (worker.joinable()) worker.join();
  return Screen::Finish;
}

} // namespace nbi
