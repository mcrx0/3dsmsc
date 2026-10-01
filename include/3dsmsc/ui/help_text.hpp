#pragma once

#include <string>
#include <vector>

namespace threedsmsc {

// A line of a static text screen. A header is a section title; on the Button mapping screen the
// other lines read "button|action".
struct HelpLine {
  std::string text;
  bool header = false;
};

// The Button mapping screen: every button and what it does, grouped by screen.
const std::vector<HelpLine>& controls_help();

// The About screen: the version, the credits, and the third-party libraries.
std::vector<HelpLine> about_help(const std::string& version);

}
