#pragma once

namespace threedsmsc {

// The screens the bottom screen can show. Home is the transport and tiles; the rest open from it.
enum class Panel {
  Home,
  Search,
  Browse,
  Queue,
  Settings,
  Folders,
  Equalizer,
  About,
  Controls,
};

// One more than the highest value: the size of an array indexed by Panel.
constexpr int panel_count = 9;

}
