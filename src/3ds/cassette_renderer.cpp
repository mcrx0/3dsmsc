#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "3dsmsc/audio/equalizer.hpp"
#include "3dsmsc/ui/layout.hpp"
#include "cassette_renderer.hpp"

namespace threedsmsc {
namespace {

// Palette constants are written 0xAARRGGBB.
u32 color(std::uint32_t argb) {
  return C2D_Color32(static_cast<u8>((argb >> 16) & 0xFFu), static_cast<u8>((argb >> 8) & 0xFFu),
                     static_cast<u8>(argb & 0xFFu), static_cast<u8>((argb >> 24) & 0xFFu));
}

}

struct Palette {
  std::uint32_t background;
  std::uint32_t background_top;
  std::uint32_t surface;
  std::uint32_t raised;
  std::uint32_t border;
  std::uint32_t text;
  std::uint32_t text_soft;
  std::uint32_t muted;
  std::uint32_t accent;
  std::uint32_t glyph;      // icons on surfaces
  std::uint32_t on_accent;  // icons on the accent colour
  std::uint32_t shadow;     // text shadow, 0 for none
};

namespace {

// The text of one half of the queue's button row.
const char* button_label(const CassetteView& view, int half) {
  if (half != 0)
    return view.shuffle ? "Shuffle: on" : "Shuffle: off";
  if (view.repeat_mode == 0)
    return "Repeat: off";
  return view.repeat_mode == 2 ? "Repeat: one" : "Repeat: all";
}

// The 3DS LCD crushes dark greys, so surfaces and borders sit well apart from the background.
constexpr Palette dark_palette = {0xFF050607, 0xFF0F1114, 0xFF252B33, 0xFF3A424E,
                                  0xFF7A8492, 0xFFFFFBF2, 0xFFEDE9DF, 0xFFC4CAD2,
                                  0xFFFF7358, 0xFFF4EAD0, 0xFF16120F, 0xFF000000};
constexpr Palette light_palette = {0xFFE4DDCD, 0xFFF4F0E6, 0xFFFFFDF8, 0xFFD2C9B3,
                                   0xFF8A826E, 0xFF101214, 0xFF2B2F34, 0xFF4B5057,
                                   0xFFB93420, 0xFF24272B, 0xFFFFF8EC, 0};
// The cassette is a physical object, so its colours do not change with the theme.
constexpr std::uint32_t cassette_cream = 0xFFE7DDC3;

// Layout constants are ints (the input code compares them with touch coordinates).
constexpr float px(int value) {
  return static_cast<float>(value);
}

std::string format_time(std::uint64_t milliseconds) {
  const std::uint64_t seconds = milliseconds / 1000;
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%u:%02u", static_cast<unsigned>(seconds / 60),
                static_cast<unsigned>(seconds % 60));
  return buffer;
}

}

bool CassetteRenderer::init() {
  text_buffer_ = C2D_TextBufNew(8192);
  if (text_buffer_ == nullptr)
    return false;
  // Icons are optional: without romfs the UI falls back to text labels.
  icons_ = C2D_SpriteSheetLoad("romfs:/gfx/icons.t3x");
  top_target_ = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  bottom_target_ = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  if (top_target_ == nullptr || bottom_target_ == nullptr) {
    shutdown();
    return false;
  }
  return true;
}

void CassetteRenderer::shutdown() {
  if (icons_ != nullptr) {
    C2D_SpriteSheetFree(icons_);
    icons_ = nullptr;
  }
  text_cache_.clear();
  cached_glyphs_ = 0;
  if (text_buffer_ != nullptr) {
    C2D_TextBufDelete(text_buffer_);
    text_buffer_ = nullptr;
  }
  if (top_target_ != nullptr) {
    C3D_RenderTargetDelete(top_target_);
    top_target_ = nullptr;
  }
  if (bottom_target_ != nullptr) {
    C3D_RenderTargetDelete(bottom_target_);
    bottom_target_ = nullptr;
  }
}

const CassetteRenderer::CachedText* CassetteRenderer::cached_text(const std::string& text) {
  const auto found = text_cache_.find(text);
  if (found != text_cache_.end())
    return &found->second;
  // The glyph buffer holds a fixed number of characters; start over when it would overflow.
  constexpr std::size_t glyph_budget = 8000;
  if (cached_glyphs_ + text.size() > glyph_budget) {
    C2D_TextBufClear(text_buffer_);
    text_cache_.clear();
    cached_glyphs_ = 0;
  }
  CachedText entry = {};
  C2D_TextParse(&entry.text, text_buffer_, text.c_str());
  C2D_TextOptimize(&entry.text);
  float height = 0.0f;
  C2D_TextGetDimensions(&entry.text, 1.0f, 1.0f, &entry.width, &height);
  cached_glyphs_ += text.size();
  return &text_cache_.emplace(text, entry).first->second;
}

void CassetteRenderer::draw_parsed(const C2D_Text* text, float x, float y, float scale,
                                   u32 text_color) {
  // citro2d draws higher z on top; text always sits above the shapes behind it. Without
  // C2D_WithColor the colour argument is ignored and the text is drawn black.
  constexpr u32 flags = C2D_AtBaseline | C2D_WithColor;
  if (shadow_color_ != 0)
    C2D_DrawText(text, flags, x + 1.0f, y + 1.0f, 0.94f, scale, scale, shadow_color_);
  C2D_DrawText(text, flags, x, y, 0.95f, scale, scale, text_color);
  // A second pass half a pixel to the right thickens thin strokes on the small screen.
  C2D_DrawText(text, flags, x + 0.5f, y, 0.95f, scale, scale, text_color);
}

void CassetteRenderer::draw_text(const char* text, float x, float y, float scale, u32 text_color) {
  if (text_buffer_ == nullptr || text[0] == '\0')
    return;
  draw_parsed(&cached_text(text)->text, x, y, scale, text_color);
}

void CassetteRenderer::round_rect(float x, float y, float w, float h, float radius, float z,
                                  u32 fill) {
  const float r = radius < w / 2.0f ? (radius < h / 2.0f ? radius : h / 2.0f) : w / 2.0f;
  C2D_DrawRectSolid(x + r, y, z, w - 2.0f * r, h, fill);
  C2D_DrawRectSolid(x, y + r, z, r, h - 2.0f * r, fill);
  C2D_DrawRectSolid(x + w - r, y + r, z, r, h - 2.0f * r, fill);
  C2D_DrawCircleSolid(x + r, y + r, z, r, fill);
  C2D_DrawCircleSolid(x + w - r, y + r, z, r, fill);
  C2D_DrawCircleSolid(x + r, y + h - r, z, r, fill);
  C2D_DrawCircleSolid(x + w - r, y + h - r, z, r, fill);
}

void CassetteRenderer::card(float x, float y, float w, float h, float radius, float z, u32 fill,
                            u32 outline) {
  round_rect(x, y, w, h, radius, z, outline);
  round_rect(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f, radius > 1.5f ? radius - 1.5f : radius,
             z + 0.01f, fill);
}

void CassetteRenderer::draw_text_fit(const std::string& text, float x, float y, float scale,
                                     float max_width, u32 text_color) {
  if (text_buffer_ == nullptr || text.empty())
    return;
  const CachedText* cached = cached_text(text);
  const float width = cached->width * scale;
  if (width <= max_width) {
    draw_parsed(&cached->text, x, y, scale, text_color);
    return;
  }
  // Estimate how many bytes fit, then back up to a UTF-8 character boundary.
  std::size_t keep = static_cast<std::size_t>(static_cast<float>(text.size()) * max_width / width);
  keep = keep > 3 ? keep - 3 : 0;
  while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80)
    --keep;
  draw_text((text.substr(0, keep) + "...").c_str(), x, y, scale, text_color);
}

bool CassetteRenderer::draw_icon(IconId icon, float x, float y, float size, u32 tint_color) {
  if (icons_ == nullptr || static_cast<std::size_t>(icon) >= C2D_SpriteSheetCount(icons_))
    return false;
  const C2D_Image image = C2D_SpriteSheetGetImage(icons_, static_cast<std::size_t>(icon));
  C2D_ImageTint tint = {};
  C2D_PlainImageTint(&tint, tint_color, 1.0f);
  const float scale = size / static_cast<float>(image.subtex->width);
  C2D_DrawImageAt(image, x, y, 0.95f, &tint, scale, scale);
  return true;
}

void CassetteRenderer::draw_top(const CassetteView& view) {
  const Palette& p = view.light_theme ? light_palette : dark_palette;
  C2D_TargetClear(top_target_, color(p.background));
  C2D_SceneBegin(top_target_);
  const u32 bg_top = color(p.background_top);
  const u32 bg_bottom = color(p.background);
  C2D_DrawRectangle(0.0f, 0.0f, 0.0f, 400.0f, 240.0f, bg_top, bg_top, bg_bottom, bg_bottom);

  // Cassette body, label and tape window.
  round_rect(37.0f, 9.0f, 326.0f, 134.0f, 12.5f, 0.05f, color(p.border));
  round_rect(39.0f, 11.0f, 322.0f, 130.0f, 11.0f, 0.06f, color(0xFF1E2024));
  round_rect(54.0f, 22.0f, 292.0f, 84.0f, 6.0f, 0.1f, color(cassette_cream));
  C2D_DrawRectSolid(54.0f, 66.0f, 0.12f, 292.0f, 6.0f, color(0xFFC8432F));
  C2D_DrawRectSolid(54.0f, 74.0f, 0.12f, 292.0f, 3.0f, color(0xFF1A1C1F));
  C2D_DrawRectSolid(54.0f, 80.0f, 0.12f, 292.0f, 6.0f, color(0xFFC8432F));
  round_rect(98.0f, 52.0f, 204.0f, 52.0f, 26.0f, 0.2f, color(0xFF141618));
  round_rect(176.0f, 60.0f, 48.0f, 36.0f, 4.0f, 0.25f, color(0xFF2B2E32));
  C2D_DrawRectSolid(200.0f, 60.0f, 0.26f, 1.5f, 36.0f, color(0xFF4A4E52));

  // Tape moves from the left reel to the right one as the track plays.
  const float progress =
      view.progress < 0.0f ? 0.0f : (view.progress > 1.0f ? 1.0f : view.progress);
  const float left_tape = 14.0f + 11.0f * (1.0f - progress);
  const float right_tape = 14.0f + 11.0f * progress;
  const float reel_y = 78.0f;
  const float reel_x[2] = {138.0f, 262.0f};
  const float tape_radius[2] = {left_tape, right_tape};
  for (int reel = 0; reel < 2; ++reel) {
    C2D_DrawCircleSolid(reel_x[reel], reel_y, 0.3f, tape_radius[reel], color(0xFF3B2B22));
    C2D_DrawCircleSolid(reel_x[reel], reel_y, 0.35f, 13.0f, color(0xFFCDD0D3));
    C2D_DrawCircleSolid(reel_x[reel], reel_y, 0.4f, 9.0f, color(0xFF141618));
    for (int tooth = 0; tooth < 6; ++tooth) {
      const float angle = view.reel_phase + static_cast<float>(tooth) * 1.0471976f;
      C2D_DrawLine(reel_x[reel] + std::cos(angle) * 6.0f, reel_y + std::sin(angle) * 6.0f,
                   color(0xFFCDD0D3), reel_x[reel] + std::cos(angle) * 12.0f,
                   reel_y + std::sin(angle) * 12.0f, color(0xFFCDD0D3), 2.5f, 0.45f);
    }
  }
  // Dark lettering on the cream label reads best without a shadow.
  const u32 saved_shadow = shadow_color_;
  shadow_color_ = 0;
  draw_text("MIXTAPE", 66.0f, 44.0f, 0.62f, color(0xFF1A1C1F));
  draw_text(view.playing ? "PLAYING" : (view.has_track ? "PAUSED" : "SIDE A"), 290.0f, 40.0f, 0.36f,
            color(view.playing ? 0xFFC8432F : 0xFF1A1C1F));
  shadow_color_ = saved_shadow;
  round_rect(108.0f, 112.0f, 184.0f, 28.0f, 5.0f, 0.1f, color(0xFF24272B));
  C2D_DrawCircleSolid(124.0f, 126.0f, 0.15f, 5.0f, color(0xFF0E0F11));
  C2D_DrawCircleSolid(276.0f, 126.0f, 0.15f, 5.0f, color(0xFF0E0F11));
  draw_text("3DSMSC", 168.0f, 131.0f, 0.5f, color(cassette_cream));

  // Now playing.
  draw_text_fit(view.has_track ? view.title : std::string("No track selected"), 24.0f, 168.0f,
                0.62f, 352.0f, color(p.text));
  draw_text_fit(view.has_track ? view.artist + "  \xC2\xB7  " + view.album
                               : std::string("Choose a music folder to scan"),
                24.0f, 188.0f, 0.42f, 352.0f, color(p.muted));
  round_rect(24.0f, 199.0f, 352.0f, 6.0f, 3.0f, 0.3f, color(p.border));
  if (progress > 0.0f)
    round_rect(24.0f, 199.0f, 352.0f * progress < 6.0f ? 6.0f : 352.0f * progress, 6.0f, 3.0f, 0.4f,
               color(p.accent));
  draw_text(format_time(view.position_ms).c_str(), 24.0f, 222.0f, 0.4f, color(p.muted));
  // Play-mode icons between the two times: lit in the accent colour when on, dim when off.
  // Repeat-one uses its own icon with the "1" drawn inside the arrow.
  const u32 mode_on = color(p.accent);
  const u32 mode_off = color(p.border);
  draw_icon(IconId::Shuffle, 171.0f, 206.0f, 20.0f, view.shuffle ? mode_on : mode_off);
  draw_icon(view.repeat_mode == 2 ? IconId::RepeatOne : IconId::Scan, 205.0f, 206.0f, 20.0f,
            view.repeat_mode != 0 ? mode_on : mode_off);

  // Time remaining, counting down as the track plays. An unknown length (0) is shown as dashes
  // rather than a misleading -0:00, and an estimated length that is a little short stops at 0.
  std::string remaining = "--:--";
  if (view.duration_ms != 0) {
    const std::uint64_t left =
        view.duration_ms > view.position_ms ? view.duration_ms - view.position_ms : 0;
    remaining = "-" + format_time(left);
  }
  draw_text(remaining.c_str(), 376.0f - static_cast<float>(remaining.size()) * 6.6f, 222.0f, 0.4f,
            color(p.muted));
}

void CassetteRenderer::draw_bottom(const CassetteView& view) {
  const Palette& p = view.light_theme ? light_palette : dark_palette;
  C2D_TargetClear(bottom_target_, color(p.background));
  C2D_SceneBegin(bottom_target_);
  C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 28.0f, color(p.surface));
  C2D_DrawRectSolid(0.0f, 28.0f, 0.0f, 320.0f, 2.0f, color(p.border));
  C2D_DrawCircleSolid(17.0f, 14.0f, 0.1f, 4.0f, color(p.accent));
  draw_text("3DSMSC", 28.0f, 20.0f, 0.5f, color(p.text));
  const std::string volume = std::to_string(std::lround(view.volume * 100.0f)) + "%";
  draw_battery(view, p);
  if (view.headphones)
    draw_icon(IconId::Headphones, 208.0f, 6.0f, 17.0f, color(p.accent));
  draw_icon(IconId::Volume, 232.0f, 6.0f, 16.0f, color(p.muted));
  draw_text(volume.c_str(), 278.0f - static_cast<float>(volume.size()) * 6.6f, 20.0f, 0.4f,
            color(p.muted));

  if (view.eq_visible)
    draw_equalizer(view);
  else if (view.list_visible)
    draw_list(view, p);
  else
    draw_home(view, p);
}

// The header reads, from the right corner towards the middle: battery, volume, headphones (the
// last only while headphones are plugged in).
// It is drawn from shapes, so it needs no entry in the icon sheet. A charging battery is accent
// coloured and a nearly empty one red; both are also readable from the fill level or the number.
void CassetteRenderer::draw_battery(const CassetteView& view, const Palette& p) {
  if (view.battery_display == 0 || view.battery_percent < 0)
    return;
  constexpr float right_edge = 312.0f;
  constexpr int low_percent = 15;
  constexpr std::uint32_t low_color = 0xFFE5484D;
  const std::uint32_t tone = view.battery_charging
                                 ? p.accent
                                 : (view.battery_percent <= low_percent ? low_color : p.glyph);
  if (view.battery_display == 2) {
    const std::string text = std::to_string(view.battery_percent) + "%";
    draw_text(text.c_str(), right_edge - static_cast<float>(text.size()) * 6.6f, 20.0f, 0.4f,
              color(tone));
    return;
  }
  const float x = right_edge - 24.0f;
  const float fill_width = 18.0f * static_cast<float>(view.battery_percent) / 100.0f;
  round_rect(x, 9.0f, 22.0f, 11.0f, 2.5f, 0.1f, color(tone));
  round_rect(x + 1.5f, 10.5f, 19.0f, 8.0f, 1.5f, 0.11f, color(p.surface));
  if (fill_width >= 1.0f)
    round_rect(x + 2.0f, 11.0f, fill_width, 7.0f, 1.0f, 0.12f, color(tone));
  C2D_DrawRectSolid(x + 22.0f, 12.0f, 0.1f, 2.0f, 5.0f, color(tone));
}

void CassetteRenderer::draw_list(const CassetteView& view, const Palette& p) {
  const bool has_back = draw_icon(IconId::Back, 10.0f, 36.0f, 20.0f, color(p.glyph));
  draw_text_fit(view.list_heading, has_back ? 38.0f : 14.0f, 52.0f, 0.5f, 200.0f, color(p.text));
  if (view.list_total != 0) {
    const std::string position =
        std::to_string(view.list_position + 1) + " / " + std::to_string(view.list_total);
    draw_text(position.c_str(), 312.0f - static_cast<float>(position.size()) * 6.5f, 52.0f, 0.36f,
              color(p.muted));
  }
  const float rows_top = px(layout::list_rows_top);
  for (std::size_t index = 0; index < view.list_count; ++index)
    draw_list_row(view, p, index,
                  rows_top + static_cast<float>(index) * px(layout::list_row_pitch));
  if (view.list_count == 0)
    draw_text(view.list_empty.c_str(), 20.0f, rows_top + 18.0f, 0.4f, color(p.muted));
  draw_scrollbar(view, p, rows_top);
  draw_text_fit(view.status, 12.0f, 230.0f, 0.36f, 296.0f, color(p.muted));
}

void CassetteRenderer::draw_list_row(const CassetteView& view, const Palette& p, std::size_t index,
                                     float y) {
  switch (view.list_check[index]) {
    case row_button_pair:
      draw_button_pair_row(view, p, index, y);
      break;
    case row_mapping:
      draw_mapping_row(view, p, index, y);
      break;
    case row_text:
      // A line of plain text (the About screen): no card, nothing to select.
      draw_text_fit(view.list_items[index], 20.0f, y + 18.0f, 0.4f, 280.0f, color(p.text_soft));
      break;
    case row_header:
      draw_section_header(view, p, index, y);
      break;
    default:
      draw_item_row(view, p, index, y);
      break;
  }
}

// Two half-width buttons on one row: repeat and shuffle. The selected row outlines the button the
// d-pad is on; a mode that is switched on is drawn in the accent colour.
void CassetteRenderer::draw_button_pair_row(const CassetteView& view, const Palette& p,
                                            std::size_t index, float y) {
  for (int half = 0; half < 2; ++half) {
    const float bx = 8.0f + static_cast<float>(half) * 150.0f;
    const bool row_selected = index == view.list_selected;
    const bool picked = row_selected && view.list_sub_selected == half;
    const bool active = half == 0 ? view.repeat_mode != 0 : view.shuffle;
    card(bx, y, 146.0f, 26.0f, 7.0f, 0.1f, color(picked ? p.raised : p.surface),
         color(picked ? p.accent : p.border));
    const u32 content = color(active ? p.accent : p.text_soft);
    draw_icon(half == 0 ? IconId::Scan : IconId::Shuffle, bx + 9.0f, y + 5.0f, 16.0f, content);
    draw_text_fit(button_label(view, half), bx + 31.0f, y + 18.0f, 0.38f, 108.0f,
                  picked ? color(p.text) : content);
  }
}

// "button|action": the button in the accent colour on the left, what it does on the right (the
// button mapping screen).
void CassetteRenderer::draw_mapping_row(const CassetteView& view, const Palette& p,
                                        std::size_t index, float y) {
  const std::string& item = view.list_items[index];
  const std::size_t bar = item.find('|');
  draw_text_fit(item.substr(0, bar), 20.0f, y + 18.0f, 0.4f, 70.0f, color(p.accent));
  if (bar != std::string::npos)
    draw_text_fit(item.substr(bar + 1), 96.0f, y + 18.0f, 0.4f, 204.0f, color(p.text_soft));
}

// A small label followed by a rule, so groups of rows read as groups.
void CassetteRenderer::draw_section_header(const CassetteView& view, const Palette& p,
                                           std::size_t index, float y) {
  const CachedText* title = cached_text(view.list_items[index]);
  const float title_width = title->width * 0.36f;
  draw_parsed(&title->text, 14.0f, y + 19.0f, 0.36f, color(p.accent));
  C2D_DrawRectSolid(14.0f + title_width + 8.0f, y + 14.0f, 0.2f, 296.0f - title_width - 14.0f, 1.5f,
                    color(p.border));
}

// A selectable row: a card, with a tick box and chevron on the folder picker's rows.
void CassetteRenderer::draw_item_row(const CassetteView& view, const Palette& p, std::size_t index,
                                     float y) {
  const bool selected = index == view.list_selected;
  if (selected) {
    card(8.0f, y, 296.0f, 26.0f, 7.0f, 0.1f, color(p.raised), color(p.accent));
  } else {
    card(8.0f, y, 296.0f, 26.0f, 7.0f, 0.1f, color(p.surface), color(p.border));
  }
  float text_x = 20.0f;
  float text_width = 276.0f;
  const int check = view.list_check[index];
  if (check >= 0) {
    draw_tick_box(p, y, check == 1);
    text_x = 46.0f;
    text_width = 232.0f;
  }
  draw_text_fit(view.list_items[index], text_x, y + 18.0f, 0.4f, text_width,
                color(selected ? p.text : p.text_soft));
}

// Checkbox on the left and a chevron on the right: tap the box to tick, the row to open.
void CassetteRenderer::draw_tick_box(const Palette& p, float y, bool checked) {
  const float box_x = 18.0f;
  const float box_y = y + 4.0f;
  round_rect(box_x, box_y, 18.0f, 18.0f, 4.0f, 0.2f, color(p.text));
  round_rect(box_x + 2.0f, box_y + 2.0f, 14.0f, 14.0f, 3.0f, 0.21f,
             color(checked ? p.accent : p.surface));
  if (checked) {
    C2D_DrawLine(box_x + 4.5f, box_y + 9.5f, color(p.on_accent), box_x + 8.0f, box_y + 13.0f,
                 color(p.on_accent), 2.5f, 0.3f);
    C2D_DrawLine(box_x + 7.5f, box_y + 13.0f, color(p.on_accent), box_x + 14.0f, box_y + 5.5f,
                 color(p.on_accent), 2.5f, 0.3f);
  }
  C2D_DrawLine(284.0f, y + 7.0f, color(p.text_soft), 291.0f, y + 13.0f, color(p.text_soft), 2.5f,
               0.3f);
  C2D_DrawLine(291.0f, y + 13.0f, color(p.text_soft), 284.0f, y + 19.0f, color(p.text_soft), 2.5f,
               0.3f);
}

void CassetteRenderer::draw_scrollbar(const CassetteView& view, const Palette& p, float rows_top) {
  if (view.list_total <= view.list_count)
    return;
  const float track_height =
      px(layout::list_row_pitch) * static_cast<float>(view.list_items.size()) - 2.0f;
  float thumb =
      track_height * static_cast<float>(view.list_count) / static_cast<float>(view.list_total);
  thumb = thumb < 14.0f ? 14.0f : thumb;
  const float travel = track_height - thumb;
  const float offset =
      travel * static_cast<float>(view.list_position) / static_cast<float>(view.list_total - 1);
  round_rect(309.0f, rows_top, 3.0f, track_height, 1.5f, 0.1f, color(p.border));
  round_rect(309.0f, rows_top + offset, 3.0f, thumb, 1.5f, 0.2f, color(p.text_soft));
}

void CassetteRenderer::draw_home(const CassetteView& view, const Palette& p) {
  // Status card.
  card(8.0f, px(layout::status_card_top), 304.0f, 34.0f, 9.0f, 0.1f, color(p.surface),
       color(p.border));
  draw_icon(IconId::Folder, 16.0f, 44.0f, 18.0f, color(p.accent));
  draw_text_fit(view.library_path, 42.0f, 50.0f, 0.34f, 262.0f, color(p.muted));
  draw_text_fit(view.status, 42.0f, 63.0f, 0.36f, 262.0f, color(p.text));
  draw_transport(view, p);
  draw_tiles(view, p);
}

// Transport: seek back, previous, play/pause, next, seek forward.
void CassetteRenderer::draw_transport(const CassetteView& view, const Palette& p) {
  const u32 glyph = color(p.glyph);
  const u32 on_accent = color(p.on_accent);
  const float centre_y = px(layout::transport_centre_y);
  const auto round_button = [&](int slot, float radius) {
    const float cx = px(layout::transport_centre_x[slot]);
    C2D_DrawCircleSolid(cx, centre_y, 0.1f, radius + 1.5f, color(p.border));
    C2D_DrawCircleSolid(cx, centre_y, 0.11f, radius, color(p.raised));
  };
  // An icon of the given size, centred on a transport button.
  const auto transport_icon = [&](IconId icon, int slot, float size, u32 tint) {
    return draw_icon(icon, px(layout::transport_centre_x[slot]) - size / 2.0f,
                     centre_y - size / 2.0f, size, tint);
  };
  round_button(0, 17.0f);
  round_button(1, 21.0f);
  C2D_DrawCircleSolid(px(layout::transport_centre_x[2]), centre_y, 0.1f, 28.0f, color(p.accent));
  round_button(3, 21.0f);
  round_button(4, 17.0f);
  if (!transport_icon(IconId::SeekBack, 0, 18.0f, glyph))
    draw_text("<<", 32.0f, 106.0f, 0.4f, glyph);
  if (!transport_icon(IconId::Prev, 1, 24.0f, glyph))
    draw_text("PREV", 86.0f, 106.0f, 0.36f, glyph);
  if (!transport_icon(view.playing ? IconId::Pause : IconId::Play, 2, 32.0f, on_accent))
    draw_text(view.playing ? "PAUSE" : "PLAY", 140.0f, 106.0f, 0.36f, on_accent);
  if (!transport_icon(IconId::Next, 3, 24.0f, glyph))
    draw_text("NEXT", 206.0f, 106.0f, 0.36f, glyph);
  if (!transport_icon(IconId::SeekForward, 4, 18.0f, glyph))
    draw_text(">>", 272.0f, 106.0f, 0.4f, glyph);
  // The seek step sits under each seek button, e.g. "10s".
  const std::string step = std::to_string(view.seek_seconds) + "s";
  const float step_width = static_cast<float>(step.size()) * 3.5f;
  draw_text(step.c_str(), px(layout::transport_centre_x[0]) - step_width, 130.0f, 0.34f,
            color(p.muted));
  draw_text(step.c_str(), px(layout::transport_centre_x[4]) - step_width, 130.0f, 0.34f,
            color(p.muted));
}

void CassetteRenderer::draw_tiles(const CassetteView& view, const Palette& p) {
  struct Tile {
    const char* label;
    std::string detail;
    IconId icon;
    float x;
    float y;
  };
  const Tile tiles[] = {
      {"SEARCH", std::to_string(view.library_tracks) + " tracks", IconId::Search,
       px(layout::tile_column_x[0]), px(layout::tile_row_y[0])},
      {"BROWSE", "artists / albums", IconId::Browse, px(layout::tile_column_x[1]),
       px(layout::tile_row_y[0])},
      {"QUEUE", std::to_string(view.queue_tracks) + " queued", IconId::Queue,
       px(layout::tile_column_x[0]), px(layout::tile_row_y[1])},
      {"SETTINGS", "library / player", IconId::Settings, px(layout::tile_column_x[1]),
       px(layout::tile_row_y[1])},
  };
  const u32 glyph = color(p.glyph);
  for (const Tile& tile : tiles) {
    card(tile.x, tile.y, px(layout::tile_width), px(layout::tile_height), 10.0f, 0.1f,
         color(p.surface), color(p.border));
    round_rect(tile.x + 8.0f, tile.y + 8.0f, 26.0f, 26.0f, 8.0f, 0.15f, color(p.raised));
    const bool has_icon = draw_icon(tile.icon, tile.x + 11.0f, tile.y + 11.0f, 20.0f, glyph);
    draw_text(tile.label, tile.x + (has_icon ? 42.0f : 10.0f), tile.y + 21.0f, 0.42f,
              color(p.text));
    draw_text_fit(tile.detail, tile.x + (has_icon ? 42.0f : 10.0f), tile.y + 35.0f, 0.34f, 100.0f,
                  color(p.muted));
  }
}

// The bar editor. Its bars, zero line, and buttons use the numbers in ui/layout.hpp, the same ones
// the input code hit-tests with.
void CassetteRenderer::draw_equalizer(const CassetteView& view) {
  const Palette& p = view.light_theme ? light_palette : dark_palette;
  const bool on = view.eq_enabled;
  const bool has_back = draw_icon(IconId::Back, 10.0f, 36.0f, 20.0f, color(p.glyph));
  draw_text("Equalizer", has_back ? 38.0f : 14.0f, 52.0f, 0.5f, color(p.text));
  // On/off switch.
  card(236.0f, 36.0f, 72.0f, 22.0f, 11.0f, 0.1f, color(on ? p.accent : p.raised), color(p.border));
  C2D_DrawCircleSolid(on ? 295.0f : 249.0f, 47.0f, 0.2f, 8.0f, color(on ? p.on_accent : p.muted));
  draw_text(on ? "ON" : "OFF", on ? 250.0f : 266.0f, 52.0f, 0.38f,
            color(on ? p.on_accent : p.text_soft));

  // Selected band readout and preset name.
  const int selected = view.eq_selected;
  const float frequency = eq_band_frequency(selected);
  const std::string band_name = frequency >= 1000.0f
                                    ? std::to_string(static_cast<int>(frequency / 1000.0f)) + " kHz"
                                    : std::to_string(static_cast<int>(frequency)) + " Hz";
  const int selected_gain = view.eq_gains[selected];
  const std::string readout =
      band_name + "  " + (selected_gain > 0 ? "+" : "") + std::to_string(selected_gain) + " dB";
  draw_text(readout.c_str(), 14.0f, 78.0f, 0.42f, color(p.text));
  draw_text_fit(view.eq_preset, 312.0f - static_cast<float>(view.eq_preset.size()) * 6.5f, 78.0f,
                0.38f, 150.0f, color(p.muted));

  constexpr float centre_y = px(layout::eq_zero_y);
  constexpr float pixels_per_db = px(layout::eq_half_range_px) / static_cast<float>(eq_max_db);
  constexpr float pitch = px(layout::eq_bars_width) / static_cast<float>(eq_band_count);
  const u32 level_color = color(on ? p.accent : p.muted);
  for (int band = 0; band < eq_band_count; ++band) {
    const float cx = px(layout::eq_bars_left) + pitch * (static_cast<float>(band) + 0.5f);
    const bool is_selected = band == selected;
    if (is_selected)
      card(cx - 14.0f, 84.0f, 28.0f, 108.0f, 8.0f, 0.1f, color(p.raised), color(p.accent));
    // Track, then the coloured span between 0 dB and the band's gain.
    round_rect(cx - 4.0f, 88.0f, 8.0f, 100.0f, 4.0f, 0.2f, color(p.border));
    round_rect(cx - 3.0f, 89.0f, 6.0f, 98.0f, 3.0f, 0.21f, color(p.surface));
    const float level_y = centre_y - static_cast<float>(view.eq_gains[band]) * pixels_per_db;
    const float top = level_y < centre_y ? level_y : centre_y;
    const float height = (level_y < centre_y ? centre_y - level_y : level_y - centre_y);
    if (height > 0.5f)
      C2D_DrawRectSolid(cx - 3.0f, top, 0.25f, 6.0f, height, level_color);
    // 0 dB tick and the knob.
    C2D_DrawRectSolid(cx - 9.0f, centre_y - 0.5f, 0.3f, 18.0f, 1.0f, color(p.muted));
    C2D_DrawCircleSolid(cx, level_y, 0.35f, 7.5f, color(p.text));
    C2D_DrawCircleSolid(cx, level_y, 0.36f, 5.0f, level_color);
    // Frequency label.
    const float f = eq_band_frequency(band);
    const std::string label = f >= 1000.0f ? std::to_string(static_cast<int>(f / 1000.0f)) + "k"
                                           : std::to_string(static_cast<int>(f));
    draw_text(label.c_str(), cx - static_cast<float>(label.size()) * 3.0f, 204.0f, 0.3f,
              color(is_selected ? p.text : p.muted));
  }

  // Buttons: next preset, reset to flat.
  const float buttons_y = px(layout::eq_buttons_top + 2);  // the touch zone starts 2 px higher
  card(8.0f, buttons_y, 148.0f, 24.0f, 8.0f, 0.1f, color(p.surface), color(p.border));
  draw_text("NEXT PRESET", 30.0f, 227.0f, 0.38f, color(p.text));
  draw_icon(IconId::Next, 14.0f, 214.0f, 16.0f, color(p.glyph));
  card(164.0f, buttons_y, 148.0f, 24.0f, 8.0f, 0.1f, color(p.surface), color(p.border));
  draw_text("RESET TO FLAT", 190.0f, 227.0f, 0.38f, color(p.text));
  draw_icon(IconId::Scan, 170.0f, 214.0f, 16.0f, color(p.glyph));
}

void CassetteRenderer::render(const CassetteView& view) {
  shadow_color_ = view.light_theme ? color(light_palette.shadow) : color(dark_palette.shadow);
  if ((view.light_theme ? light_palette.shadow : dark_palette.shadow) == 0)
    shadow_color_ = 0;
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  draw_top(view);
  draw_bottom(view);
  C2D_Flush();
  C3D_FrameEnd(0);
}

}
