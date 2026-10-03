#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "citro2d.h"
#include "tex3ds.h"
static FILE* out;
static int cur = 0;
static C3D_RenderTarget* const TOP = (C3D_RenderTarget*)1;
static C3D_RenderTarget* const BOT = (C3D_RenderTarget*)2;
void stub_open(const char* p) {
  out = fopen(p, "w");
}
void stub_close() {
  fclose(out);
}
void stub_frame(const char* n) {
  fprintf(out, "F %s\n", n);
}
C2D_TextBuf C2D_TextBufNew(int) {
  return (C2D_TextBuf)1;
}
void C2D_TextBufDelete(C2D_TextBuf) {}
void C2D_TextBufClear(C2D_TextBuf) {}
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t s, gfx3dSide_t) {
  return s == GFX_TOP ? TOP : BOT;
}
void C3D_RenderTargetDelete(C3D_RenderTarget*) {}
void C2D_TextParse(C2D_Text* t, C2D_TextBuf, const char* s) {
  strncpy(t->s, s, 511);
  t->s[511] = 0;
}
void C2D_TextOptimize(C2D_Text*) {}
void C2D_DrawText(const C2D_Text* t, int, float x, float y, float z, float sx, float, u32 c) {
  fprintf(out, "T %d %f %f %f %f %u %s\n", cur, x, y, z, sx, c, t->s);
}
u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) {
  return (u32)a << 24 | (u32)b << 16 | (u32)g << 8 | r;
}
void C2D_TargetClear(C3D_RenderTarget* t, u32 c) {
  fprintf(out, "C %d %u\n", t == TOP ? 0 : 1, c);
}
void C2D_SceneBegin(C3D_RenderTarget* t) {
  cur = t == TOP ? 0 : 1;
  fprintf(out, "S %d\n", cur);
}
void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 c) {
  fprintf(out, "R %d %f %f %f %f %f %u\n", cur, x, y, z, w, h, c);
}
void C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 a, u32 b, u32 c, u32 d) {
  fprintf(out, "G %d %f %f %f %f %f %u %u %u %u\n", cur, x, y, z, w, h, a, b, c, d);
}
void C2D_DrawCircleSolid(float x, float y, float z, float r, u32 c) {
  fprintf(out, "O %d %f %f %f %f %u\n", cur, x, y, z, r, c);
}
bool C2D_DrawLine(float x0, float y0, u32, float x1, float y1, u32 c, float th, float z) {
  fprintf(out, "L %d %f %f %f %f %f %f %u\n", cur, x0, y0, x1, y1, th, z, c);
  return true;
}
void C2D_TextGetDimensions(const C2D_Text* t, float sx, float, float* w, float* h) {
  *w = strlen(t->s) * 15.0f * sx;
  *h = 30 * sx;
}
#include <chrono>
#include <thread>
void C2D_Flush() {}
void C3D_FrameBegin(int) {}
void C3D_FrameEnd(int) {
  static int ms = std::getenv("FRAME_MS") ? std::atoi(std::getenv("FRAME_MS")) : 16;
  if (ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
// The cover texture: real memory of the right size, so the sanitizers check what the renderer
// copies.
bool C3D_TexInit(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR) {
  tex->width = width;
  tex->height = height;
  tex->data = std::calloc(static_cast<size_t>(width) * height, 2);
  return true;
}
void C3D_TexSetFilter(C3D_Tex*, GPU_TEXTURE_FILTER_PARAM, GPU_TEXTURE_FILTER_PARAM) {}
void C3D_TexLoadImage(C3D_Tex* tex, const void* data, GPU_TEXFACE, int) {
  std::memcpy(tex->data, data, static_cast<size_t>(tex->width) * tex->height * 2);
}
void C3D_TexDelete(C3D_Tex* tex) {
  std::free(tex->data);
  tex->data = nullptr;
}
C2D_SpriteSheet C2D_SpriteSheetLoad(const char*) {
  return (C2D_SpriteSheet)1;
}
void C2D_SpriteSheetFree(C2D_SpriteSheet) {}
unsigned long C2D_SpriteSheetCount(C2D_SpriteSheet) {
  return 18;
}
static Subtex st{32, 32};
C2D_Image C2D_SpriteSheetGetImage(C2D_SpriteSheet, unsigned long i) {
  return C2D_Image{(C3D_Tex*)i, &st};
}
void C2D_PlainImageTint(C2D_ImageTint* t, u32 c, float) {
  t->c = c;
}
bool C2D_DrawImageAt(C2D_Image im, float x, float y, float z, const C2D_ImageTint* t, float sx,
                     float) {
  if (im.subtex->width == 64) {  // the cover thumbnail, drawn from the real texture
    fprintf(out, "K %d %f %f %f %f\n", cur, x, y, z, sx);
    return true;
  }
  fprintf(out, "I %d %f %f %f %f %lu %u\n", cur, x, y, z, sx, (unsigned long)im.tex, t->c);
  return true;
}
void Tex3DS_TextureFree(Tex3DS_Texture) {}
Tex3DS_Texture Tex3DS_TextureImportStdio(FILE*, C3D_Tex*, void*, bool) {
  return nullptr;
}
int Tex3DS_GetNumSubTextures(Tex3DS_Texture) {
  return 0;
}
const Subtex* Tex3DS_GetSubTexture(Tex3DS_Texture, int) {
  return nullptr;
}
