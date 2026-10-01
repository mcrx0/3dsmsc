
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "3ds.h"
#include "3ds/applets/swkbd.h"
#include "citro2d.h"
void stub_open(const char*);
namespace {
int frame = 0;
int max_frames = 200;
struct Ev {
  u32 keys;
  int x, y;
};
std::map<int, std::vector<Ev>> script;
u32 down = 0, held = 0;
touchPosition pen{0, 0};
struct Init {
  Init() {
    stub_open(std::getenv("CMDS") ? std::getenv("CMDS") : "/dev/null");
    if (const char* f = std::getenv("FRAMES"))
      max_frames = std::atoi(f);
    if (const char* k = std::getenv("KEYS")) {  // "10:A,20:TOUCH@100,110"
      std::string all = k;
      size_t pos = 0;
      while (pos < all.size()) {
        size_t end = all.find(',', pos);
        if (end == std::string::npos)
          end = all.size();
        std::string tok = all.substr(pos, end - pos);
        // TOUCH@x,y contains a comma: glue the y part back
        if (tok.find("TOUCH@") != std::string::npos && tok.find(',') == std::string::npos) {
          size_t e2 = all.find(',', end + 1);
          if (e2 == std::string::npos)
            e2 = all.size();
          tok += "," + all.substr(end + 1, e2 - end - 1);
          end = e2;
        }
        pos = end + 1;
        int fr = std::atoi(tok.c_str());
        std::string name = tok.substr(tok.find(':') + 1);
        Ev ev{0, 0, 0};
        if (name == "A")
          ev.keys = KEY_A;
        else if (name == "B")
          ev.keys = KEY_B;
        else if (name == "X")
          ev.keys = KEY_X;
        else if (name == "Y")
          ev.keys = KEY_Y;
        else if (name == "UP")
          ev.keys = KEY_DUP;
        else if (name == "DOWN")
          ev.keys = KEY_DDOWN;
        else if (name == "LEFT")
          ev.keys = KEY_DLEFT;
        else if (name == "RIGHT")
          ev.keys = KEY_DRIGHT;
        else if (name.rfind("TOUCH@", 0) == 0) {
          ev.keys = KEY_TOUCH;
          std::sscanf(name.c_str() + 6, "%d,%d", &ev.x, &ev.y);
        }
        script[fr].push_back(ev);
      }
    }
  }
} init;
}
bool aptMainLoop() {
  ++frame;
  if (frame > max_frames)
    return false;
  return true;
}
void hidScanInput() {
  down = 0;
  held = 0;
  auto it = script.find(frame);
  if (it != script.end())
    for (auto& e : it->second) {
      down |= e.keys;
      held |= e.keys;
      if (e.keys == KEY_TOUCH) {
        pen.px = e.x;
        pen.py = e.y;
      }
    }
}
u32 hidKeysDown() {
  return down;
}
u32 hidKeysHeld() {
  return held;
}
u32 hidKeysUp() {
  return 0;
}
void hidTouchRead(touchPosition* t) {
  *t = pen;
}
void gfxInitDefault() {}
void gfxExit() {}
void gfxFlushBuffers() {}
void gfxSwapBuffers() {}
void gspWaitForVBlank() {}
void consoleInit(gfxScreen_t, void*) {}
bool C3D_Init(int) {
  return true;
}
bool C2D_Init(int) {
  return true;
}
void C2D_Prepare() {}
void C2D_Fini() {}
void C3D_Fini() {}
Result romfsInit() {
  return 0;
}
Result romfsExit() {
  return 0;
}
u64 osGetTime() {
  if (const char* d = std::getenv("DETERMINISTIC"); d && d[0] == '1')
    return static_cast<u64>(frame) * 16;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
void svcSleepThread(long long ns) {
  std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
void swkbdInit(SwkbdState*, int, int, int) {}
void swkbdSetFeatures(SwkbdState*, int) {}
void swkbdSetHintText(SwkbdState*, const char*) {}
void swkbdSetInitialText(SwkbdState*, const char*) {}
int swkbdInputText(SwkbdState*, char*, unsigned long) {
  return 0;
}
Result ndspInit() {
  if (const char* n = std::getenv("NOAUDIO"); n && n[0] == '1')
    return -1;
  return 0;
}
void ndspExit() {}
void ndspSetOutputMode(int) {}
void ndspChnSetInterp(int, int) {}
void ndspChnSetRate(int, float) {}
void ndspChnSetFormat(int, int) {}
void ndspChnSetMix(int, float*) {}
void ndspChnWaveBufClear(int) {}
void ndspChnWaveBufAdd(int, ndspWaveBuf* b) {
  b->status = NDSP_WBUF_DONE;
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
void ndspChnSetPaused(int, bool) {}
void* linearAlloc(unsigned long n) {
  return std::malloc(n);
}
void linearFree(void* p) {
  std::free(p);
}
void DSP_FlushDataCache(const void*, unsigned long) {}
void aptSetSleepAllowed(bool) {}
Thread threadCreate(void (*fn)(void*), void* arg, unsigned long, int, int, bool) {
  return new std::thread(fn, arg);
}
Result threadJoin(Thread t, u64) {
  static_cast<std::thread*>(t)->join();
  return 0;
}
void threadFree(Thread t) {
  delete static_cast<std::thread*>(t);
}

Result DSP_GetHeadphoneStatus(bool* in) {
  const char* e = std::getenv("HEADPHONES");
  *in = e && e[0] == '1';
  return 0;
}

// Battery: BATTERY=<percent> (default 80); CHARGING=1 reports the charger connected.
Result mcuHwcInit() {
  return 0;
}
void mcuHwcExit() {}
Result MCUHWC_GetBatteryLevel(u8* level) {
  const char* value = std::getenv("BATTERY");
  *level = static_cast<u8>(value != nullptr ? std::atoi(value) : 80);
  return 0;
}
Result ptmuInit() {
  return 0;
}
void ptmuExit() {}
Result PTMU_GetBatteryChargeState(u8* charging) {
  const char* value = std::getenv("CHARGING");
  *charging = value != nullptr && value[0] == '1' ? 1 : 0;
  return 0;
}
