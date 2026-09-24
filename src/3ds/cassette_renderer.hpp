#pragma once

#include <citro2d.h>
#include <tex3ds.h>

#include <string>

#include "3dsmsc/ui/cassette_view.hpp"

namespace threedsmsc {

class CassetteRenderer {
 public:
  bool init();
  void shutdown();
  void render(const CassetteView& view);

 private:
  void draw_text(const char* text, float x, float y, float scale, u32 color);
  void draw_top(const CassetteView& view);
  void draw_bottom(const CassetteView& view);
  void load_artwork(const std::string& path);

  C2D_TextBuf text_buffer_ = nullptr;
  C2D_Text text_ = {};
  C3D_RenderTarget* top_target_ = nullptr;
  C3D_RenderTarget* bottom_target_ = nullptr;
  C3D_Tex artwork_texture_ = {};
  Tex3DS_Texture artwork_import_ = nullptr;
  C2D_Image artwork_image_ = {};
  std::string artwork_path_;
};

}
