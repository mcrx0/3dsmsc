#pragma once

#include <citro2d.h>
#include <tex3ds.h>

#include <string>
#include <unordered_map>

#include "3dsmsc/ui/cassette_view.hpp"
#include "icons.hpp"

namespace threedsmsc {

struct Palette;

class CassetteRenderer {
 public:
  bool init();
  void shutdown();
  void render(const CassetteView& view);

 private:
  void draw_text(const char* text, float x, float y, float scale, u32 color);
  void round_rect(float x, float y, float w, float h, float radius, float z, u32 color);
  // Draws text shortened with "..." so it fits within max_width pixels.
  // Rounded rectangle with a 1.5px outline, so cards stay visible on similar backgrounds.
  void card(float x, float y, float w, float h, float radius, float z, u32 fill, u32 outline);
  void draw_text_fit(const std::string& text, float x, float y, float scale, float max_width,
                     u32 color);
  // Returns false (and draws nothing) when the icon sheet is not available.
  bool draw_icon(IconId icon, float x, float y, float size, u32 color);
  struct CachedText {
    C2D_Text text;
    float width;  // at scale 1.0
  };
  // Parsing text is costly on the ARM11, so each distinct string is parsed once and reused.
  const CachedText* cached_text(const std::string& text);
  void draw_parsed(const C2D_Text* text, float x, float y, float scale, u32 color);
  void draw_top(const CassetteView& view);
  void draw_bottom(const CassetteView& view);
  void draw_equalizer(const CassetteView& view);
  void draw_battery(const CassetteView& view, const Palette& p);
  void draw_list(const CassetteView& view, const Palette& p);
  void draw_list_row(const CassetteView& view, const Palette& p, std::size_t index, float y);
  void draw_button_pair_row(const CassetteView& view, const Palette& p, std::size_t index, float y);
  void draw_mapping_row(const CassetteView& view, const Palette& p, std::size_t index, float y);
  void draw_section_header(const CassetteView& view, const Palette& p, std::size_t index, float y);
  void draw_item_row(const CassetteView& view, const Palette& p, std::size_t index, float y);
  void draw_tick_box(const Palette& p, float y, bool checked);
  void draw_scrollbar(const CassetteView& view, const Palette& p, float rows_top);
  void draw_home(const CassetteView& view, const Palette& p);
  void draw_transport(const CassetteView& view, const Palette& p);
  void draw_tiles(const CassetteView& view, const Palette& p);

  u32 shadow_color_ = 0;  // 0 disables the text shadow
  C2D_SpriteSheet icons_ = nullptr;
  C2D_TextBuf text_buffer_ = nullptr;
  std::unordered_map<std::string, CachedText> text_cache_;
  std::size_t cached_glyphs_ = 0;
  C3D_RenderTarget* top_target_ = nullptr;
  C3D_RenderTarget* bottom_target_ = nullptr;
};

}
