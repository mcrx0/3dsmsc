#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "cassette_renderer.hpp"

namespace threedsmsc {
namespace {

u32 color(std::uint32_t rgba) {
  return C2D_Color32(static_cast<u8>((rgba >> 24) & 0xFFu), static_cast<u8>((rgba >> 16) & 0xFFu),
                     static_cast<u8>((rgba >> 8) & 0xFFu), static_cast<u8>(rgba & 0xFFu));
}

}

bool CassetteRenderer::init() {
  text_buffer_ = C2D_TextBufNew(4096);
  if (text_buffer_ == nullptr)
    return false;
  top_target_ = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  bottom_target_ = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  if (top_target_ == nullptr || bottom_target_ == nullptr) {
    shutdown();
    return false;
  }
  return true;
}

void CassetteRenderer::shutdown() {
  if (artwork_import_ != nullptr) {
    Tex3DS_TextureFree(artwork_import_);
    artwork_import_ = nullptr;
    C3D_TexDelete(&artwork_texture_);
  }
  artwork_image_ = {};
  artwork_path_.clear();
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

void CassetteRenderer::draw_text(const char* text, float x, float y, float scale, u32 text_color) {
  if (text_buffer_ == nullptr)
    return;
  C2D_TextParse(&text_, text_buffer_, text);
  C2D_TextOptimize(&text_);
  C2D_DrawText(&text_, C2D_AtBaseline, x, y, 0.0f, scale, scale, text_color);
}

void CassetteRenderer::load_artwork(const std::string& path) {
  if (path == artwork_path_)
    return;
  if (artwork_import_ != nullptr) {
    Tex3DS_TextureFree(artwork_import_);
    artwork_import_ = nullptr;
    C3D_TexDelete(&artwork_texture_);
  }
  artwork_image_ = {};
  artwork_path_ = path;
  if (path.empty())
    return;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr)
    return;
  C3D_Tex imported_texture = {};
  Tex3DS_Texture imported = Tex3DS_TextureImportStdio(file, &imported_texture, nullptr, false);
  std::fclose(file);
  if (imported == nullptr || Tex3DS_GetNumSubTextures(imported) == 0) {
    if (imported != nullptr)
      Tex3DS_TextureFree(imported);
    return;
  }
  artwork_texture_ = imported_texture;
  artwork_import_ = imported;
  artwork_image_.tex = &artwork_texture_;
  artwork_image_.subtex = Tex3DS_GetSubTexture(imported, 0);
}

void CassetteRenderer::draw_top(const CassetteView& view) {
  C2D_TargetClear(top_target_, color(0xFF080A0C));
  C2D_SceneBegin(top_target_);
  C2D_DrawRectSolid(20.0f, 20.0f, 0.0f, 360.0f, 150.0f, color(0xFF17191B));
  C2D_DrawRectSolid(34.0f, 34.0f, 0.1f, 332.0f, 80.0f, color(0xFFE7DDC3));
  C2D_DrawRectSolid(34.0f, 92.0f, 0.2f, 332.0f, 8.0f, color(0xFFB43A2D));
  C2D_DrawRectSolid(34.0f, 104.0f, 0.2f, 332.0f, 5.0f, color(0xFF17191B));
  C2D_DrawRectSolid(150.0f, 48.0f, 0.3f, 100.0f, 40.0f, color(0xFF45494A));
  C2D_DrawRectSolid(160.0f, 53.0f, 0.4f, 80.0f, 30.0f, color(0xFFB6A58A));
  C2D_DrawCircleSolid(120.0f, 100.0f, 0.5f, 33.0f, color(0xFF111315));
  C2D_DrawCircleSolid(120.0f, 100.0f, 0.6f, 24.0f, color(0xFFB9BDC0));
  C2D_DrawCircleSolid(120.0f, 100.0f, 0.7f, 12.0f, color(0xFF111315));
  C2D_DrawCircleSolid(280.0f, 100.0f, 0.5f, 33.0f, color(0xFF111315));
  C2D_DrawCircleSolid(280.0f, 100.0f, 0.6f, 24.0f, color(0xFFB9BDC0));
  C2D_DrawCircleSolid(280.0f, 100.0f, 0.7f, 12.0f, color(0xFF111315));
  for (int reel = 0; reel < 2; ++reel) {
    const float center_x = reel == 0 ? 120.0f : 280.0f;
    for (int spoke = 0; spoke < 3; ++spoke) {
      const float angle = view.reel_phase + static_cast<float>(spoke) * 2.0943951f;
      C2D_DrawCircleSolid(center_x + std::cos(angle) * 17.0f, 100.0f + std::sin(angle) * 17.0f,
                          0.8f, 4.0f, color(0xFF45494A));
    }
  }
  draw_text("MIXTAPE", 48.0f, 67.0f, 1.0f, color(0xFF17191B));
  draw_text("3DSMSC", 48.0f, 143.0f, 0.75f, color(0xFFE7DDC3));
  draw_text(view.playing     ? "PLAYING"
            : view.has_track ? "READY"
                             : "NO TRACK",
            270.0f, 143.0f, 0.45f, color(view.playing ? 0xFFB43A2D : 0xFFB9BDC0));
  draw_text(view.has_track ? view.title.c_str() : "No track selected", 34.0f, 187.0f, 0.55f,
            color(0xFFF4F0E6));
  draw_text(view.has_track ? view.artist.c_str() : "Choose a music folder to scan", 34.0f, 207.0f,
            0.42f, color(0xFFB9BDC0));
  const float progress =
      view.progress < 0.0f ? 0.0f : (view.progress > 1.0f ? 1.0f : view.progress);
  C2D_DrawRectSolid(34.0f, 220.0f, 0.8f, 332.0f, 3.0f, color(0xFF34383A));
  C2D_DrawRectSolid(34.0f, 220.0f, 0.9f, 332.0f * progress, 3.0f, color(0xFFB43A2D));
}

void CassetteRenderer::draw_bottom(const CassetteView& view) {
  C2D_TargetClear(bottom_target_, color(0xFF101214));
  C2D_SceneBegin(bottom_target_);
  const u32 active = color(0xFFB43A2D);
  const u32 inactive = color(0xFFB9BDC0);
  const u32 panel_background = color(0xFF1A1D20);
  const auto panel_color = [&](int index) {
    return view.selected_panel == index ? active : inactive;
  };
  C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 30.0f, color(0xFF1A1D20));
  draw_text("3DSMSC", 12.0f, 21.0f, 0.55f, color(0xFFF4F0E6));
  draw_text("0.4.2b", 268.0f, 21.0f, 0.32f, color(0xFFB9BDC0));
  C2D_DrawRectSolid(8.0f, 42.0f, 0.1f, 304.0f, 50.0f, panel_background);
  draw_text("LIBRARY", 12.0f, 58.0f, 0.45f, panel_color(0));
  draw_text("sdmc:/3dmms/music/", 12.0f, 74.0f, 0.4f, color(0xFFF4F0E6));
  draw_text(view.status.c_str(), 12.0f, 88.0f, 0.32f, color(0xFFB9BDC0));
  C2D_DrawRectSolid(12.0f, 100.0f, 0.1f, 90.0f, 30.0f, panel_background);
  C2D_DrawRectSolid(108.0f, 100.0f, 0.1f, 96.0f, 30.0f, panel_background);
  C2D_DrawRectSolid(210.0f, 100.0f, 0.1f, 90.0f, 30.0f, panel_background);
  draw_text("PREV", 32.0f, 120.0f, 0.36f, color(0xFFB9BDC0));
  draw_text(view.playing ? "PAUSE" : "PLAY", 133.0f, 120.0f, 0.36f, color(0xFFB9BDC0));
  draw_text("NEXT", 233.0f, 120.0f, 0.36f, color(0xFFB9BDC0));
  C2D_DrawRectSolid(12.0f, 140.0f, 0.1f, 140.0f, 42.0f, panel_background);
  C2D_DrawRectSolid(164.0f, 140.0f, 0.1f, 140.0f, 42.0f, panel_background);
  C2D_DrawRectSolid(12.0f, 190.0f, 0.1f, 140.0f, 42.0f, panel_background);
  C2D_DrawRectSolid(164.0f, 190.0f, 0.1f, 140.0f, 42.0f, panel_background);
  draw_text("SEARCH", 18.0f, 167.0f, 0.4f, panel_color(1));
  draw_text("ALBUMS", 170.0f, 167.0f, 0.4f, panel_color(2));
  draw_text("QUEUE", 18.0f, 217.0f, 0.4f, panel_color(3));
  draw_text("SETTINGS", 170.0f, 217.0f, 0.4f, panel_color(4));
  if (view.list_visible) {
    C2D_DrawRectSolid(8.0f, 42.0f, 0.2f, 304.0f, 190.0f, color(0xFF101214));
    draw_text(view.selected_panel == 1
                  ? (view.search_query.empty() ? "SEARCH RESULTS" : view.search_query.c_str())
              : view.selected_panel == 2 ? "ALBUMS"
                                         : "QUEUE",
              16.0f, 60.0f, 0.45f, color(0xFFB43A2D));
    for (std::size_t index = 0; index < view.list_count; ++index) {
      const float y = 88.0f + static_cast<float>(index) * 28.0f;
      if (index == view.list_selected)
        C2D_DrawRectSolid(12.0f, y - 17.0f, 0.3f, 296.0f, 24.0f, color(0xFF45494A));
      draw_text(view.list_items[index].c_str(), 18.0f, y, 0.36f, color(0xFFF4F0E6));
    }
    if (view.list_count == 0)
      draw_text("No matching tracks", 18.0f, 88.0f, 0.36f, color(0xFFB9BDC0));
  }
}

void CassetteRenderer::render(const CassetteView& view) {
  C2D_TextBufClear(text_buffer_);
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  draw_top(view);
  draw_bottom(view);
  C2D_Flush();
  C3D_FrameEnd(0);
}

}
