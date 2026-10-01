#!/usr/bin/env python3
"""Rasterize the app's SVG artwork with librsvg and libcairo (through ctypes, no pip packages).

Usage:
  build-icons.py [output-dir]   UI icons: assets/icons/*.svg -> 32x32 PNGs plus the tex3ds atlas
                                list (default output: build/icons). Run by the CMake build.
  build-icons.py --app-icon     The launcher icon: assets/icon.svg -> assets/icon.png (48x48).
                                Run it by hand after editing assets/icon.svg and commit the PNG.

librsvg and libcairo are present on most desktop Linux installs.
"""
import ctypes
import ctypes.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "assets", "icons")
UI_ICON_SIZE = 32
APP_ICON_SIZE = 48  # the size smdhtool expects for the launcher icon


class Dimensions(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int),
                ("em", ctypes.c_double), ("ex", ctypes.c_double)]


def load(name):
    path = ctypes.util.find_library(name)
    if not path:
        sys.exit(f"error: lib{name} not found; install librsvg2 and libcairo2")
    return ctypes.CDLL(path)


class Rasterizer:
    def __init__(self):
        self.cairo, self.rsvg, self.gobject = load("cairo"), load("rsvg-2"), load("gobject-2.0")
        cairo, rsvg = self.cairo, self.rsvg
        cairo.cairo_image_surface_create.restype = ctypes.c_void_p
        cairo.cairo_image_surface_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        cairo.cairo_create.restype = ctypes.c_void_p
        cairo.cairo_create.argtypes = [ctypes.c_void_p]
        cairo.cairo_scale.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]
        cairo.cairo_surface_write_to_png.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
        cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
        rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
        rsvg.rsvg_handle_new_from_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
        rsvg.rsvg_handle_get_dimensions.argtypes = [ctypes.c_void_p, ctypes.POINTER(Dimensions)]
        rsvg.rsvg_handle_render_cairo.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.gobject.g_object_unref.argtypes = [ctypes.c_void_p]

    def render(self, svg_path, size, png_path):
        handle = self.rsvg.rsvg_handle_new_from_file(svg_path.encode(), None)
        if not handle:
            sys.exit(f"error: could not read {svg_path}")
        dims = Dimensions()
        self.rsvg.rsvg_handle_get_dimensions(handle, ctypes.byref(dims))
        surface = self.cairo.cairo_image_surface_create(0, size, size)  # CAIRO_FORMAT_ARGB32
        context = self.cairo.cairo_create(surface)
        self.cairo.cairo_scale(context, size / dims.width, size / dims.height)
        if not self.rsvg.rsvg_handle_render_cairo(handle, context):
            sys.exit(f"error: could not render {svg_path}")
        if self.cairo.cairo_surface_write_to_png(surface, png_path.encode()):
            sys.exit(f"error: could not write {png_path}")
        self.cairo.cairo_destroy(context)
        self.cairo.cairo_surface_destroy(surface)
        self.gobject.g_object_unref(handle)


def build_ui_icons(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rasterizer = Rasterizer()
    with open(os.path.join(ICON_DIR, "order.txt")) as handle:
        names = [line.strip() for line in handle if line.strip()]
    for name in names:
        rasterizer.render(os.path.join(ICON_DIR, name + ".svg"), UI_ICON_SIZE,
                          os.path.join(out_dir, name + ".png"))
    with open(os.path.join(out_dir, "icons.t3s"), "w") as t3s:
        t3s.write("--atlas -f rgba8888 -z auto\n")
        t3s.writelines(name + ".png\n" for name in names)
    print(f"wrote {len(names)} icons to {out_dir}")


def build_app_icon():
    source = os.path.join(ROOT, "assets", "icon.svg")
    target = os.path.join(ROOT, "assets", "icon.png")
    Rasterizer().render(source, APP_ICON_SIZE, target)
    print(f"wrote {target} ({APP_ICON_SIZE}x{APP_ICON_SIZE})")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--app-icon":
        build_app_icon()
    else:
        build_ui_icons(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "icons"))


if __name__ == "__main__":
    main()
