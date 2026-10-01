#pragma once
#include "citro2d.h"
#include <cstdio>
typedef void* Tex3DS_Texture; void Tex3DS_TextureFree(Tex3DS_Texture); Tex3DS_Texture Tex3DS_TextureImportStdio(FILE*, C3D_Tex*, void*, bool);
int Tex3DS_GetNumSubTextures(Tex3DS_Texture); const Subtex* Tex3DS_GetSubTexture(Tex3DS_Texture,int);
