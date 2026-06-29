// screens.hpp — the five wizard screens. Each builds its components, runs the
// shared ScreenInteractive loop, and returns the next Screen to route to.
#pragma once
#include "app.hpp"

namespace ftxui { class ScreenInteractive; }

namespace nbi {
Screen run_mode(ftxui::ScreenInteractive&, AppState&);
Screen run_disk(ftxui::ScreenInteractive&, AppState&);
Screen run_account(ftxui::ScreenInteractive&, AppState&);
Screen run_install(ftxui::ScreenInteractive&, AppState&);
Screen run_finish(ftxui::ScreenInteractive&, AppState&);
} // namespace nbi
