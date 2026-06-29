// theme.hpp — the amber-CRT look + a shared screen chrome.
//
// Truecolor amber-on-black, matching the design doc. The whole screen is
// painted amber/black at the outer edge; bright amber / green / red are the
// only accents. The default FTXUI menu/button focus is inverted, so on this
// palette the selection bar renders as black-on-amber for free.
#pragma once
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <utility>
#include <vector>

namespace nbi::theme {
using namespace ftxui;

inline const Color amber       = Color::RGB(0xff, 0x94, 0x16);
inline const Color amberBright = Color::RGB(0xff, 0xb4, 0x54);
inline const Color amberDim    = Color::RGB(0xa8, 0x5e, 0x0e);
inline const Color bg          = Color::RGB(0x00, 0x00, 0x00);
inline const Color ok          = Color::RGB(0x39, 0xd3, 0x53);
inline const Color bad         = Color::RGB(0xff, 0x4f, 0x4f);

inline Element title(const std::string& s) { return text(s) | color(amberBright) | bold; }
inline Element hint(const std::string& s)  { return text(s) | color(amberDim); }

// A keycap hint bar, e.g. keys({{"↑↓","move"},{"Enter","select"}}).
inline Element keys(std::vector<std::pair<std::string, std::string>> ks) {
  Elements e;
  for (auto& k : ks) {
    if (!k.first.empty())
      e.push_back(text(" " + k.first + " ") | color(bg) | bgcolor(amberBright) | bold);
    e.push_back(text(" " + k.second + "   ") | color(amberDim));
  }
  return hbox(std::move(e));
}

// Standard amber chrome around a screen body: masthead + heading + body + keys.
inline Element chrome(const std::string& heading,
                      const std::string& sub,
                      Element body,
                      std::vector<std::pair<std::string, std::string>> ks) {
  auto masthead = hbox({
      text(" NextBSD Installer ") | color(bg) | bgcolor(amber) | bold,
      filler(),
      hint(" " + sub + " "),
  });
  return vbox({
             masthead,
             separator() | color(amberDim),
             title(heading),
             text(""),
             std::move(body) | flex,
             separator() | color(amberDim),
             keys(std::move(ks)),
         })
         | borderRounded | color(amber) | bgcolor(bg);
}

} // namespace nbi::theme
