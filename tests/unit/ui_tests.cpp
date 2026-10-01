#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "3dsmsc/audio/aac_decoder.hpp"
#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/audio/flac_decoder.hpp"
#include "3dsmsc/audio/mp3_decoder.hpp"
#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/track.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "3dsmsc/ui/equalizer_editor.hpp"
#include "3dsmsc/ui/help_text.hpp"
#include "3dsmsc/ui/layout.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using test::entry;
using test::FakeFileSystem;
using test::make_track;

void test_browse_navigator() {
  assert(threedsmsc::list_window_start(3, 2, 5) == 0);
  assert(threedsmsc::list_window_start(100, 0, 5) == 0);
  assert(threedsmsc::list_window_start(100, 50, 5) == 48);
  assert(threedsmsc::list_window_start(100, 99, 5) == 95);

  threedsmsc::LibraryIndex library{};
  const auto add = [&](const char* title, const char* artist, const char* album, std::uint16_t n) {
    threedsmsc::Track track{};
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.track_number = n;
    library.tracks.push_back(track);
  };
  add("Two", "Band", "First", 2);
  add("One", "Band", "First", 1);
  add("Solo", "Band", "Second", 1);
  add("Other", "Other", "Third", 1);
  const threedsmsc::BrowseIndex index = threedsmsc::build_browse_index(library);
  threedsmsc::BrowseNavigator navigator;
  navigator.reset(&library, &index);
  threedsmsc::BrowseSelection playback;
  assert(navigator.level() == threedsmsc::BrowseLevel::Artists);
  assert(navigator.count() == 2);
  navigator.move(-1);
  assert(navigator.selected() == 1);
  navigator.move(1);
  assert(navigator.selected() == 0);
  assert(!navigator.enter(playback));
  assert(navigator.heading() == "Band");
  assert(navigator.count() == 2);
  assert(navigator.label(0) == "First");
  assert(!navigator.enter(playback));
  assert(navigator.heading() == "First");
  assert(navigator.label(0) == "01. One");
  assert(navigator.label(1) == "02. Two");
  navigator.move(1);
  assert(navigator.enter(playback));
  assert(playback.start == 1);
  assert(playback.tracks.size() == 2);
  assert(library.tracks[playback.tracks[playback.start]].title == "Two");
  assert(navigator.back());
  assert(navigator.level() == threedsmsc::BrowseLevel::Albums);
  assert(navigator.back());
  assert(!navigator.back());
  assert(navigator.level() == threedsmsc::BrowseLevel::Artists);

  threedsmsc::BrowseNavigator empty;
  assert(empty.count() == 0);
  assert(!empty.enter(playback));
  empty.move(1);
}

void test_equalizer_editor_mapping() {
  using threedsmsc::EqualizerEditor;
  // Touch x -> bar: ten bars of 29.2 px starting at x 14.
  assert(EqualizerEditor::band_at(0) == 0);  // left of the first bar
  assert(EqualizerEditor::band_at(14) == 0);
  assert(EqualizerEditor::band_at(43) == 0);  // still inside the first bar
  assert(EqualizerEditor::band_at(44) == 1);
  assert(EqualizerEditor::band_at(159) == 4);  // (159 - 14) / 29.2 is just under 5
  assert(EqualizerEditor::band_at(160) == 5);  // and 160 is exactly the start of bar 5
  assert(EqualizerEditor::band_at(305) == 9);
  assert(EqualizerEditor::band_at(400) == 9);  // right of the last bar
  // Touch y -> gain: 0 dB at y 138, 12 dB every 50 px, clamped.
  assert(EqualizerEditor::gain_at(138) == 0);
  assert(EqualizerEditor::gain_at(88) == 12);
  assert(EqualizerEditor::gain_at(188) == -12);
  assert(EqualizerEditor::gain_at(113) == 6);
  assert(EqualizerEditor::gain_at(137) == 0);  // one pixel is a quarter of a dB: rounds to 0
  assert(EqualizerEditor::gain_at(0) == 12);   // above the top
  assert(EqualizerEditor::gain_at(239) == -12);
}

void test_equalizer_editor_actions() {
  threedsmsc::Settings settings = threedsmsc::default_settings();
  threedsmsc::EqualizerEditor editor(settings);
  assert(editor.selected() == 5);
  for (int step = 0; step < 6; ++step)
    editor.select_next();
  assert(editor.selected() == 1);  // wrapped past the last band
  editor.select_previous();
  editor.select_previous();
  assert(editor.selected() == 9);  // and back past the first
  editor.select(3);
  editor.select(99);  // out of range: ignored
  assert(editor.selected() == 3);
  assert(!editor.take_unsaved());

  // The first edit switches the equalizer on; repeating the same gain changes nothing.
  assert(!settings.eq_enabled);
  assert(editor.raise());
  assert(settings.eq_enabled && settings.eq_gains[3] == 1);
  assert(editor.take_unsaved() && !editor.take_unsaved());
  assert(!editor.set_gain(3, 1));
  assert(!editor.take_unsaved());  // an unchanged gain is not a pending save
  assert(editor.lower() && settings.eq_gains[3] == 0);
  // Gains stay within the allowed range.
  assert(editor.set_gain(3, 99) && settings.eq_gains[3] == threedsmsc::eq_max_db);
  assert(editor.set_gain(3, -99) && settings.eq_gains[3] == threedsmsc::eq_min_db);
  assert(!editor.set_gain(-1, 5) && !editor.set_gain(10, 5));
  // With the equalizer off, setting the gain it already has still switches it on.
  settings.eq_enabled = false;
  assert(editor.set_gain(3, threedsmsc::eq_min_db) && settings.eq_enabled);

  assert(editor.toggle() && !settings.eq_enabled);
  assert(editor.toggle() && settings.eq_enabled);

  std::string status;
  assert(!editor.take_status(status));
  settings.eq_enabled = false;
  assert(editor.reset());
  assert(settings.eq_gains[3] == 0 && !settings.eq_enabled);  // reset flattens but does not enable
  assert(editor.take_status(status) && status == "Equalizer reset to flat");
  assert(!editor.take_status(status));
  assert(editor.next_preset());
  assert(settings.eq_enabled && status.empty() == false);
  assert(editor.take_status(status) && status.rfind("Preset: ", 0) == 0);
  assert(std::string(threedsmsc::eq_preset_name(settings.eq_gains)) != "Custom");

  editor.begin_drag(7);
  assert(editor.dragging() == 7 && editor.selected() == 7);
  editor.end_drag();
  assert(editor.dragging() == -1);
}

void test_help_text() {
  const auto& controls = threedsmsc::controls_help();
  assert(controls.size() > 10 && controls.front().header && controls.front().text == "HOME");
  int headers = 0;
  for (const threedsmsc::HelpLine& line : controls) {
    const std::size_t bar = line.text.find('|');
    if (line.header) {
      ++headers;
      assert(bar == std::string::npos);  // a section title has no button/action split
    } else {
      assert(bar != std::string::npos && bar > 0 && bar + 1 < line.text.size());
    }
  }
  assert(headers >= 4);

  const auto about = threedsmsc::about_help("1.2.3");
  bool has_version = false;
  bool has_credit = false;
  bool has_faad = false;
  for (const threedsmsc::HelpLine& line : about) {
    has_version = has_version || line.text == "Version 1.2.3";
    has_credit = has_credit || line.text.find("@mcrx0") != std::string::npos;
    has_faad = has_faad || line.text.find("FAAD2") != std::string::npos;
  }
  assert(has_version && has_credit && has_faad);
}

// The renderer draws with these numbers and the input code hit-tests with them, so they must
// describe the same buttons: a button has to be where it looks like it is.
void test_layout_is_consistent() {
  namespace layout = threedsmsc::layout;
  // Between two neighbouring transport buttons lies the edge of their touch zones.
  for (int index = 0; index < 4; ++index) {
    const int midpoint =
        (layout::transport_centre_x[index] + layout::transport_centre_x[index + 1]) / 2;
    assert(layout::transport_zone_edge[index] == midpoint);
  }
  assert(layout::transport_centre_y > layout::transport_top &&
         layout::transport_centre_y < layout::transport_bottom);
  // The tiles sit inside the screen and their touch zones cover them.
  for (int column = 0; column < 2; ++column) {
    assert(layout::tile_column_x[column] + layout::tile_width <= 320);
    assert(layout::tile_split_x > layout::tile_column_x[0] + layout::tile_width - 1);
    assert(layout::tile_split_x <= layout::tile_column_x[1]);
  }
  for (int row = 0; row < 2; ++row) {
    assert(layout::tile_zone_bottom[row] > layout::tile_row_y[row]);
    assert(layout::tile_row_y[row] + layout::tile_height >= layout::tile_zone_bottom[row] - 4);
  }
  // The list rows end above the footer, and a tap on the title bar cannot hit a row.
  assert(layout::list_title_bottom < layout::list_rows_top);
  assert(layout::list_rows_top + layout::list_row_count * layout::list_row_pitch <= 230);
  // The equalizer's bars and buttons stay on screen and do not overlap.
  assert(layout::eq_bars_left + layout::eq_bars_width <= 320);
  assert(layout::eq_zero_y - layout::eq_half_range_px >= layout::eq_bars_touch_top);
  assert(layout::eq_zero_y + layout::eq_half_range_px <= layout::eq_bars_touch_bottom);
  assert(layout::eq_bars_touch_bottom <= layout::eq_buttons_top);
  assert(layout::eq_header_bottom <= layout::eq_bars_touch_top);
}

}

void run_ui_tests() {
  test_browse_navigator();
  test_equalizer_editor_mapping();
  test_equalizer_editor_actions();
  test_help_text();
  test_layout_is_consistent();
}
