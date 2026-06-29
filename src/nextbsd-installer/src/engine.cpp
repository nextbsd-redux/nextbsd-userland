#include "engine.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace nbi::engine {

std::string slugify(const std::string& in) {
  std::string s;
  for (unsigned char c : in) {
    char l = static_cast<char>(std::tolower(c));
    if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9'))
      s += l;
    else if (l == ' ' || l == '-' || l == '_')
      s += '-';
  }
  // collapse runs of '-'
  std::string o;
  bool prev_dash = false;
  for (char c : s) {
    if (c == '-') {
      if (!prev_dash) o += c;
      prev_dash = true;
    } else {
      o += c;
      prev_dash = false;
    }
  }
  while (!o.empty() && o.front() == '-') o.erase(o.begin());
  while (!o.empty() && o.back() == '-') o.pop_back();
  if (o.size() > 63) o.resize(63);  // RFC-1123 label cap
  return o;
}

std::string suggest_hostname(const std::string& user, const std::string& model) {
  std::string u = slugify(user.empty() ? "nextbsd" : user);
  std::string m = slugify(model);
  return m.empty() ? (u + "-nextbsd") : (u + "-" + m);
}

void probe(AppState& st) {
  if (st.demo) {
    st.build_id = "continuous · 2026-06-08 00:19 UTC · 0d5f191";
    st.dmi_model = "ThinkPad T460";
    st.media_dev = "da0";
    st.disks = {
        {"ada0", "238 GB", "Samsung SSD 860 EVO", false,
         {{"\"BACKUP\"", "212 GB", "ufs", ""},
          {"\"EFI\"", "260 MB", "fat32", "(ESP)"}}},
        {"ada1", "931 GB", "WDC WD10EZEX-08WN4A0", false,
         {{"\"vault\"", "900 GB", "zfs", "(zpool: tank)"},
          {"\"swap\"", "16 GB", "freebsd-swap", ""}}},
        {"da0", "15 GB", "SanDisk Ultra USB", true,
         {{"\"NEXTBSD_INST\"", "15 GB", "iso9660", ""}}},
    };
    st.existing = {false, "", ""};
    return;
  }
  // Real probe runs $LIBEXEC/probe-disks.sh and parses its output. Left for the
  // FreeBSD path (the demo branch is what runs off-target); wired next pass.
}

bool run_install(AppState& st,
                 const std::function<void(int, const std::string&)>& progress) {
  if (st.demo || st.dry_run) {
    struct Step { int to; const char* msg; };
    const std::array<Step, 5> steps = {{
        {8,   "Partitioning target disk (GPT: ESP + freebsd-ufs)…"},
        {15,  "Creating UFS filesystem (label ROOTFS)…"},
        {88,  "Cloning base with cpdup…"},
        {95,  "Installing bootcode + populating ESP…"},
        {100, "Configuring account, hostname, host keys…"},
    }};
    int cur = 0;
    for (const auto& s : steps) {
      while (cur < s.to) {
        ++cur;
        progress(cur, s.msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(st.demo ? 22 : 1));
      }
    }
    st.success = true;
    return true;
  }
  // Real run execs $LIBEXEC/do-install.sh and parses "PROGRESS n" / "STATUS …"
  // lines from its stdout. Wired in the next pass alongside probe-disks.sh.
  st.fail_stage = "engine not yet wired for on-target install";
  return false;
}

} // namespace nbi::engine
