// engine.hpp — the bridge between the FTXUI front-end and the /bin/sh engine.
//
// Probing and the install run are delegated to shell scripts under
// $NEXTBSD_INSTALLER_LIBEXEC (default NBI_LIBEXEC_DEFAULT). In --demo mode the
// engine synthesizes data and simulates the run so the whole UI is exercisable
// off-FreeBSD with no hardware.
#pragma once
#include "app.hpp"
#include <functional>
#include <string>

namespace nbi::engine {

// Hostname helpers (the design's slug + suggestion logic).
std::string slugify(const std::string& in);
std::string suggest_hostname(const std::string& user, const std::string& model);

// Fill st.disks / st.existing / st.dmi_model / st.media_dev / st.build_id.
void probe(AppState& st);

// Run the install, reporting progress(percent 0..100, status line).
// Returns true on success; on failure sets st.fail_stage.
bool run_install(AppState& st,
                 const std::function<void(int, const std::string&)>& progress);

} // namespace nbi::engine
