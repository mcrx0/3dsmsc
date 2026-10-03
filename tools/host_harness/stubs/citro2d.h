#pragma once
#include "3ds.h"
struct C2D_TextBuf_s; typedef C2D_TextBuf_s* C2D_TextBuf; struct C2D_Text{ char s[512]; }; struct C3D_RenderTarget; struct C3D_Tex{ void* data; u16 width,height; };
struct Tex3DS_SubTexture{ u16 width,height; float left,top,right,bottom; }; typedef Tex3DS_SubTexture Subtex; struct C2D_Image{ C3D_Tex* tex; const Subtex* subtex;}; struct C2D_ImageTint{ u32 c; };
typedef void* C2D_SpriteSheet; C2D_SpriteSheet C2D_SpriteSheetLoad(const char*); void C2D_SpriteSheetFree(C2D_SpriteSheet); unsigned long C2D_SpriteSheetCount(C2D_SpriteSheet);
C2D_Image C2D_SpriteSheetGetImage(C2D_SpriteSheet, unsigned long); void C2D_PlainImageTint(C2D_ImageTint*, u32, float);
bool C2D_DrawImageAt(C2D_Image, float,float,float,const C2D_ImageTint*, float, float);
C2D_TextBuf C2D_TextBufNew(int); void C2D_TextBufDelete(C2D_TextBuf); void C2D_TextBufClear(C2D_TextBuf);
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t, gfx3dSide_t); void C3D_RenderTargetDelete(C3D_RenderTarget*);
void C2D_TextParse(C2D_Text*, C2D_TextBuf, const char*); void C2D_TextOptimize(C2D_Text*);
enum { C2D_AtBaseline=1, C2D_WithColor=2 }; void C2D_DrawText(const C2D_Text*, int, float,float,float,float,float,u32);
u32 C2D_Color32(u8,u8,u8,u8); void C2D_TargetClear(C3D_RenderTarget*, u32); void C2D_SceneBegin(C3D_RenderTarget*);
void C2D_DrawRectSolid(float,float,float,float,float,u32); void C2D_DrawCircleSolid(float,float,float,float,u32);
void C2D_Flush(); void C3D_FrameBegin(int); void C3D_FrameEnd(int); enum { C3D_FRAME_SYNCDRAW=1 }; void C3D_TexDelete(C3D_Tex*);
enum GPU_TEXCOLOR { GPU_RGB565 = 3 }; enum GPU_TEXFACE { GPU_TEXFACE_2D = 0 }; enum GPU_TEXTURE_FILTER_PARAM { GPU_LINEAR = 1 };
bool C3D_TexInit(C3D_Tex*, u16, u16, GPU_TEXCOLOR); void C3D_TexSetFilter(C3D_Tex*, GPU_TEXTURE_FILTER_PARAM, GPU_TEXTURE_FILTER_PARAM); void C3D_TexLoadImage(C3D_Tex*, const void*, GPU_TEXFACE, int);
void C2D_DrawRectangle(float,float,float,float,float,u32,u32,u32,u32);
bool C2D_DrawLine(float,float,u32,float,float,u32,float,float);
void C2D_TextGetDimensions(const C2D_Text*, float, float, float*, float*);
