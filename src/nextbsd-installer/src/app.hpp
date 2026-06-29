// app.hpp — installer state shared across the wizard screens.
#pragma once
#include <string>
#include <vector>

namespace nbi {

enum class Screen {
  Mode, Disk, Account, Install, Finish,   // wizard steps
  Quit, Reboot, Shutdown, Shell,          // terminal actions
};
enum class Mode { Install, Upgrade };

struct Volume {
  std::string label, size, fstype, note;
};

struct Disk {
  std::string dev;        // ada0 / nvd0 / vtbd0 — the unambiguous handle
  std::string size;       // human readable
  std::string model;
  bool is_media = false;  // the live install medium — shown but not selectable
  std::vector<Volume> volumes;
};

struct ExistingRoot {
  bool found = false;
  std::string dev;        // e.g. ada0p2
  std::string version;    // e.g. "NextBSD Server 2026-05-20"
};

struct AppState {
  bool demo = false;      // synthetic probe data, simulated install (no hardware)
  bool dry_run = false;   // engine prints actions instead of executing them

  std::string build_id = "continuous · (unknown build)";
  std::string media_dev;  // the booted install medium, if any
  std::string dmi_model;  // DMI product, for the hostname suggestion

  std::vector<Disk> disks;
  ExistingRoot existing;

  // user choices
  Mode mode = Mode::Install;
  int  disk_index = 0;
  std::string username, password, confirm, hostname;
  bool hostname_dirty = false;   // operator edited hostname -> stop auto-deriving

  // result
  bool success = false;
  std::string fail_stage;
};

} // namespace nbi
