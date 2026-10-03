#pragma once

// Where things are on the bottom screen (320 x 240 pixels). The renderer draws with these and the
// input code hit-tests with the same numbers, so the two cannot drift apart: a button is where it
// looks like it is.

namespace threedsmsc::layout {

constexpr int header_height = 28;

// List screens: a title bar, then rows of equal height.
constexpr int list_title_bottom = 66;  // a tap above this goes back
constexpr int list_rows_top = 68;
constexpr int list_row_pitch = 28;
constexpr int list_row_count = 5;    // rows shown at once
constexpr int tick_zone_right = 56;  // a tap left of this x ticks a folder instead of opening it
constexpr int button_pair_split =
    158;  // on the queue's button row: repeat on the left, shuffle right

// Top screen, now playing: the cover frame sits left of the title and artist, which move right to
// make room for it. It ends above the progress bar.
constexpr int cover_frame_x = 22;
constexpr int cover_frame_y = 150;
constexpr int cover_frame_size = 44;
constexpr int cover_text_x = 74;

// Home screen.
constexpr int status_card_top = 36;
constexpr int status_card_bottom = 72;
constexpr int transport_top = 72;
constexpr int transport_bottom = 134;
constexpr int transport_centre_y = 102;
// Seek back, previous, play/pause, next, seek forward.
constexpr int transport_centre_x[5] = {40, 100, 160, 220, 280};
constexpr int transport_zone_edge[4] = {70, 130, 190, 250};  // between neighbouring buttons
constexpr int tile_width = 148;
constexpr int tile_height = 42;
constexpr int tile_column_x[2] = {8, 164};
constexpr int tile_split_x = 160;
constexpr int tile_row_y[2] = {142, 190};
constexpr int tile_zone_bottom[2] = {186, 234};

// Equalizer editor: ten bars, 0 dB at eq_zero_y, +-12 dB spanning eq_half_range_px each way.
constexpr int eq_header_bottom = 62;
constexpr int eq_back_right = 120;   // the back arrow area
constexpr int eq_switch_left = 232;  // the on/off switch area
constexpr int eq_bars_left = 14;
constexpr int eq_bars_width = 292;
constexpr int eq_bars_touch_top = 80;
constexpr int eq_bars_touch_bottom = 198;
constexpr int eq_zero_y = 138;
constexpr int eq_half_range_px = 50;
constexpr int eq_buttons_top = 208;  // next preset on the left, reset on the right
constexpr int eq_buttons_split = 160;

}
