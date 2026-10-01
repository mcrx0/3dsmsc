#include <string>
#include <vector>

#include "3dsmsc/ui/help_text.hpp"

namespace threedsmsc {

const std::vector<HelpLine>& controls_help() {
  static const std::vector<HelpLine> lines = {
      {"HOME", true},
      {"A|Play or pause", false},
      {"X|Next track", false},
      {"Y|Previous track", false},
      {"D-pad L/R|Seek back / forward", false},
      {"L / R|Volume down / up", false},
      {"START|Exit the app", false},
      {"LISTS", true},
      {"D-pad U/D|Move up / down", false},
      {"D-pad L/R|Page up / down", false},
      {"A|Open or select", false},
      {"B|Go back", false},
      {"X|Edit search, tick a folder", false},
      {"QUEUE", true},
      {"D-pad L/R|Pick repeat or shuffle", false},
      {"A|Switch the picked mode", false},
      {"EQUALIZER", true},
      {"D-pad L/R|Choose a band", false},
      {"D-pad U/D|Raise / lower the gain", false},
      {"A|Switch on or off", false},
      {"X|Next preset", false},
      {"Y|Reset to flat", false},
      {"B|Go back", false},
      {"TOUCH", true},
      {"Tap|Select a row; tap again to open", false},
      {"Drag|Move an equalizer bar", false},
  };
  return lines;
}

std::vector<HelpLine> about_help(const std::string& version) {
  return {
      {"3DSMSC", true},
      {"Version " + version, false},
      {"An offline music player for the 3DS", false},
      {"CREDITS", true},
      {"Created by @mcrx0", false},
      {"and a lot of iterations with LLMs", false},
      {"THIRD PARTY", true},
      {"minimp3, minimp4 (CC0)", false},
      {"dr_flac (Unlicense / MIT-0)", false},
      {"FAAD2 (GPL-2.0 or later)", false},
      {"libctru, citro2d, citro3d (zlib)", false},
      {"Built with devkitPro", false},
  };
}

}
