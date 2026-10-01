#pragma once
#include <cstdint>
typedef uint32_t u32; typedef uint8_t u8; typedef uint16_t u16; typedef uint64_t u64; typedef int32_t s32; typedef int64_t s64;
typedef void* Thread; typedef int Result;
inline bool R_SUCCEEDED(Result r){return r>=0;} inline bool R_FAILED(Result r){return r<0;}
struct touchPosition { u16 px, py; };
enum { KEY_A=1,KEY_B=2,KEY_START=4,KEY_L=8,KEY_R=16,KEY_LEFT=32,KEY_RIGHT=64,KEY_DUP=128,KEY_DDOWN=256,KEY_DLEFT=512,KEY_DRIGHT=1024,KEY_X=2048,KEY_Y=4096,KEY_TOUCH=8192,KEY_ZL=16384 };
bool aptMainLoop(); void hidScanInput(); u32 hidKeysDown(); u32 hidKeysHeld(); u32 hidKeysUp(); void hidTouchRead(touchPosition*);
void gfxInitDefault(); void gfxExit(); void gfxFlushBuffers(); void gfxSwapBuffers(); void gspWaitForVBlank();
enum gfxScreen_t { GFX_TOP, GFX_BOTTOM }; enum gfx3dSide_t { GFX_LEFT };
void consoleInit(gfxScreen_t, void*);
struct ndspWaveBuf { void* data_vaddr; unsigned nsamples; int status; };
#define NDSP_FORMAT_STEREO_PCM16 1
enum { NDSP_OUTPUT_STEREO=1, NDSP_INTERP_LINEAR=1, NDSP_WBUF_DONE=3, NDSP_WBUF_QUEUED=1 };
Result ndspInit(); void ndspExit(); void ndspSetOutputMode(int); void ndspChnSetInterp(int,int); void ndspChnSetRate(int,float);
void ndspChnSetFormat(int,int); void ndspChnSetMix(int,float*); void ndspChnWaveBufClear(int); void ndspChnWaveBufAdd(int,ndspWaveBuf*); void ndspChnSetPaused(int,bool);
void* linearAlloc(unsigned long); void linearFree(void*); void DSP_FlushDataCache(const void*, unsigned long);
Thread threadCreate(void(*)(void*), void*, unsigned long, int, int, bool); Result threadJoin(Thread,u64); void threadFree(Thread);
#define U64_MAX 0xFFFFFFFFFFFFFFFFull
void aptSetSleepAllowed(bool);
bool C3D_Init(int); bool C2D_Init(int); void C2D_Prepare(); void C2D_Fini(); void C3D_Fini();
#define C3D_DEFAULT_CMDBUF_SIZE 1
#define C2D_DEFAULT_MAX_OBJECTS 1
Result romfsInit(); Result romfsExit(); u64 osGetTime(); void svcSleepThread(long long);

Result DSP_GetHeadphoneStatus(bool*);
Result mcuHwcInit(); void mcuHwcExit(); Result MCUHWC_GetBatteryLevel(u8*);
Result ptmuInit(); void ptmuExit(); Result PTMU_GetBatteryChargeState(u8*);
