#include <3ds.h>

#include <sys/stat.h>

#include <cstdio>
#include <memory>

#include "app.hpp"
#include "boot_log.hpp"
#include "cassette_renderer.hpp"

namespace {

// Shows a message on the text console. Call only after the renderers are shut down,
// because citro3d and the console cannot share a screen.
void show_startup_error(const char* message) {
  threedsmsc::boot_log(message);
  consoleInit(GFX_TOP, nullptr);
  std::printf("%s\nPress START to exit.\n", message);
  while (aptMainLoop()) {
    hidScanInput();
    if ((hidKeysDown() & KEY_START) != 0)
      break;
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }
}

}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  using threedsmsc::boot_log;
  gfxInitDefault();
  // First run: make sure the data folder and default music folder exist.
  mkdir("sdmc:/3dsmsc", 0777);
  mkdir("sdmc:/3dsmsc/music", 0777);
  std::remove("sdmc:/3dsmsc/boot.log");
  boot_log("3dsmsc " APP_VERSION ": gfx ready");
  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    show_startup_error("3D renderer initialization failed.");
    gfxExit();
    return 1;
  }
  boot_log("citro3d ready");
  if (!C2D_Init(8192)) {
    C3D_Fini();
    show_startup_error("2D renderer initialization failed.");
    gfxExit();
    return 1;
  }
  boot_log("citro2d ready");
  C2D_Prepare();
  // romfs holds the optional icon sheet; the UI still works without it.
  const bool romfs_mounted = R_SUCCEEDED(romfsInit());
  boot_log(romfs_mounted ? "romfs mounted" : "romfs unavailable (text labels)");
  threedsmsc::CassetteRenderer renderer;
  if (!renderer.init()) {
    C2D_Fini();
    C3D_Fini();
    show_startup_error("UI renderer initialization failed.");
    gfxExit();
    return 1;
  }
  boot_log("renderer ready");
  {
    threedsmsc::CassetteView boot_view;
    boot_view.artist = "Offline library player";
    boot_view.album = "New Nintendo 3DS";
    boot_view.status = "Starting audio...";
    renderer.render(boot_view);
  }
  boot_log("first frame drawn");
  {
    // The app holds the player's 16 KB decode buffer and every list, and the main thread's stack
    // is only 32 KB, so it lives on the heap.
    const std::unique_ptr<threedsmsc::App> app(new threedsmsc::App(renderer));
    app->run();
  }
  renderer.shutdown();
  if (romfs_mounted)
    romfsExit();
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  return 0;
}
