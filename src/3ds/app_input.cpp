#include <3ds.h>
#include <3ds/applets/swkbd.h>

#include <cstddef>
#include <string>

#include "3dsmsc/ui/layout.hpp"
#include "app.hpp"

namespace threedsmsc {

void App::apply_equalizer(bool changed) {
  if (!changed)
    return;
  audio_.set_equalizer(settings_.eq_enabled, settings_.eq_gains);
  std::string status;
  if (equalizer_.take_status(status))
    view_.status = status;
  sync_equalizer_view();
}

void App::save_equalizer() {
  if (equalizer_.take_unsaved())
    save_settings();
}

// Left/right pick a band, up/down change it, A switches on/off, X next preset, Y reset, B back.
// The stylus taps the switch, the back arrow, or the buttons, and drags a bar to set its gain.
void App::handle_equalizer_input(std::uint32_t keys_down) {
  if ((keys_down & KEY_DLEFT) != 0)
    equalizer_.select_previous();
  if ((keys_down & KEY_DRIGHT) != 0)
    equalizer_.select_next();
  if ((keys_down & KEY_DUP) != 0)
    apply_equalizer(equalizer_.raise());
  if ((keys_down & KEY_DDOWN) != 0)
    apply_equalizer(equalizer_.lower());
  if ((keys_down & KEY_A) != 0) {
    apply_equalizer(equalizer_.toggle());
    save_equalizer();
  }
  if ((keys_down & KEY_X) != 0) {
    apply_equalizer(equalizer_.next_preset());
    save_equalizer();
  }
  if ((keys_down & KEY_Y) != 0) {
    apply_equalizer(equalizer_.reset());
    save_equalizer();
  }
  if ((keys_down & KEY_B) != 0)
    go_back();
  handle_equalizer_touch(keys_down);
}

void App::handle_equalizer_touch(std::uint32_t keys_down) {
  const bool touching = (hidKeysHeld() & KEY_TOUCH) != 0;
  touchPosition pen = {};
  if (touching)
    hidTouchRead(&pen);
  if ((keys_down & KEY_TOUCH) != 0) {
    if (pen.py < layout::eq_header_bottom && pen.px >= layout::eq_switch_left) {
      apply_equalizer(equalizer_.toggle());
      save_equalizer();
    } else if (pen.py < layout::eq_header_bottom && pen.px < layout::eq_back_right) {
      go_back();
    } else if (pen.py >= layout::eq_buttons_top) {
      apply_equalizer(pen.px < layout::eq_buttons_split ? equalizer_.next_preset()
                                                        : equalizer_.reset());
      save_equalizer();
    } else if (pen.py >= layout::eq_bars_touch_top && pen.py < layout::eq_bars_touch_bottom) {
      equalizer_.begin_drag(EqualizerEditor::band_at(pen.px));
    }
  }
  if (equalizer_.dragging() >= 0 && touching)
    apply_equalizer(equalizer_.set_gain(equalizer_.dragging(), EqualizerEditor::gain_at(pen.py)));
  if (equalizer_.dragging() >= 0 && !touching) {
    equalizer_.end_drag();
    save_equalizer();
  }
  if (panel() == Panel::Equalizer)
    sync_equalizer_view();
}

void App::handle_list_input(std::uint32_t keys_down) {
  const int page = static_cast<int>(view_.list_items.size());
  if ((keys_down & KEY_DUP) != 0)
    move_cursor(-1);
  if ((keys_down & KEY_DDOWN) != 0)
    move_cursor(1);
  const bool on_button_pair = panel() == Panel::Queue && cursor_of(Panel::Queue) == 0;
  if ((keys_down & KEY_DLEFT) != 0) {
    if (on_button_pair)
      queue_toggle_ = 0;  // pick the repeat button
    else
      move_cursor(-page);
  }
  if ((keys_down & KEY_DRIGHT) != 0) {
    if (on_button_pair)
      queue_toggle_ = 1;  // pick the shuffle button
    else
      move_cursor(page);
  }
  if ((keys_down & KEY_B) != 0) {
    go_back();
  } else if ((keys_down & KEY_A) != 0) {
    if (panel() == Panel::Search && view_.search_query.empty())
      edit_search_query();
    else
      activate_row();
  }
  if ((keys_down & KEY_X) != 0) {
    if (panel() == Panel::Search)
      edit_search_query();
    else if (panel() == Panel::Folders)
      toggle_folder(cursor_of(Panel::Folders));
  }
  if (keys_down != 0)  // rebuilding the visible rows every frame is wasted work
    refresh_list();
}

void App::handle_home_input(std::uint32_t keys_down) {
  if ((keys_down & KEY_LEFT) != 0) {
    playback_.seek(-1);
    sync_playback();
  }
  if ((keys_down & KEY_RIGHT) != 0) {
    playback_.seek(1);
    sync_playback();
  }
  if ((keys_down & KEY_A) != 0) {
    playback_.toggle_play_pause();
    sync_playback();
  }
  if ((keys_down & KEY_X) != 0) {
    playback_.change_track(true);
    sync_playback();
  }
  if ((keys_down & KEY_Y) != 0) {
    playback_.change_track(false);
    sync_playback();
  }
}

void App::handle_list_touch(const touchPosition& touch) {
  if (touch.py < layout::list_title_bottom) {
    go_back();
  } else if (touch.py >= layout::list_rows_top &&
             touch.py < layout::list_rows_top +
                            layout::list_row_pitch * static_cast<int>(view_.list_items.size())) {
    const std::size_t row =
        static_cast<std::size_t>(touch.py - layout::list_rows_top) / layout::list_row_pitch;
    if (row < view_.list_count) {
      const std::size_t first = panel_cursor() - view_.list_selected;
      const std::size_t target = first + row;
      if (row_is_header(target) || panel() == Panel::About || panel() == Panel::Controls) {
        // A section title or a line of a text screen: nothing to select.
      } else if (panel() == Panel::Queue && target == 0) {
        // The button pair acts on the first tap: left half repeat, right half shuffle.
        queue_toggle_ = touch.px < layout::button_pair_split ? 0 : 1;
        cursor_of(Panel::Queue) = 0;
        if (queue_toggle_ == 0)
          cycle_repeat();
        else
          toggle_shuffle();
      } else if (panel() == Panel::Folders && touch.px < layout::tick_zone_right &&
                 target >= folder_row_first) {
        cursor_of(Panel::Folders) = target;  // the tick box toggles without opening
        toggle_folder(target);
      } else if (target == panel_cursor()) {
        activate_row();
      } else if (panel() == Panel::Browse) {
        navigator_.select(target);
      } else {
        cursor_of(panel()) = target;
      }
    }
  }
  refresh_list();
}

void App::handle_home_touch(const touchPosition& touch) {
  using namespace layout;
  if (touch.py >= status_card_top && touch.py < status_card_bottom) {
    open_panel(Panel::Folders);  // the status card shows the scan folders
  } else if (touch.py >= transport_top && touch.py < transport_bottom) {
    // Five buttons: seek back, previous, play/pause, next, seek forward.
    if (touch.px < transport_zone_edge[0]) {
      playback_.seek(-1);
    } else if (touch.px < transport_zone_edge[1]) {
      playback_.change_track(false);
    } else if (touch.px < transport_zone_edge[2]) {
      playback_.toggle_play_pause();
    } else if (touch.px < transport_zone_edge[3]) {
      playback_.change_track(true);
    } else {
      playback_.seek(1);
    }
    sync_playback();
  } else if (touch.py >= tile_row_y[0] && touch.py < tile_zone_bottom[0]) {
    open_panel(touch.px < tile_split_x ? Panel::Search : Panel::Browse);
  } else if (touch.py >= tile_row_y[1] && touch.py < tile_zone_bottom[1]) {
    open_panel(touch.px < tile_split_x ? Panel::Queue : Panel::Settings);
  }
}

void App::edit_search_query() {
  SwkbdState keyboard;
  swkbdInit(&keyboard, SWKBD_TYPE_QWERTY, 2, 63);
  swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY);
  swkbdSetHintText(&keyboard, "Search music");
  swkbdSetInitialText(&keyboard, view_.search_query.c_str());
  char query[64] = {};
  if (swkbdInputText(&keyboard, query, sizeof(query)) == SWKBD_BUTTON_CONFIRM) {
    view_.search_query = query;
    search_dirty_ = true;
    cursor_of(Panel::Search) = 0;
  }
  refresh_list();
}

}
