#!/usr/bin/env python3
"""Rasterize assets/icons/*.svg to 32x32 PNGs and write the tex3ds atlas list.

Usage: build-icons.py [output-dir]   (default: build/icons)

Needs librsvg and libcairo on the host (present on most desktop Linux installs); they are
called through ctypes so no Python packages are required.
"""
import ctypes
import ctypes.util
import os
import sys

SIZE = 32
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "assets", "icons")


def load(name):
    path = ctypes.util.find_library(name)
    if not path:
        sys.exit(f"error: lib{name} not found; install librsvg2 and libcairo2")
    return ctypes.CDLL(path)


class Dimensions(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int),
                ("em", ctypes.c_double), ("ex", ctypes.c_double)]


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "icons")
    os.makedirs(out_dir, exist_ok=True)
    cairo, rsvg, gobject = load("cairo"), load("rsvg-2"), load("gobject-2.0")
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
    gobject.g_object_unref.argtypes = [ctypes.c_void_p]

    with open(os.path.join(ICON_DIR, "order.txt")) as handle:
        names = [line.strip() for line in handle if line.strip()]
    for name in names:
        svg = os.path.join(ICON_DIR, name + ".svg")
        handle = rsvg.rsvg_handle_new_from_file(svg.encode(), None)
        if not handle:
            sys.exit(f"error: could not read {svg}")
        dims = Dimensions()
        rsvg.rsvg_handle_get_dimensions(handle, ctypes.byref(dims))
        surface = cairo.cairo_image_surface_create(0, SIZE, SIZE)  # CAIRO_FORMAT_ARGB32
        context = cairo.cairo_create(surface)
        cairo.cairo_scale(context, SIZE / dims.width, SIZE / dims.height)
        if not rsvg.rsvg_handle_render_cairo(handle, context):
            sys.exit(f"error: could not render {svg}")
        if cairo.cairo_surface_write_to_png(surface, os.path.join(out_dir, name + ".png").encode()):
            sys.exit(f"error: could not write {name}.png")
        cairo.cairo_destroy(context)
        cairo.cairo_surface_destroy(surface)
        gobject.g_object_unref(handle)

    with open(os.path.join(out_dir, "icons.t3s"), "w") as t3s:
        t3s.write("--atlas -f rgba8888 -z auto\n")
        t3s.writelines(name + ".png\n" for name in names)
    print(f"wrote {len(names)} icons to {out_dir}")


if __name__ == "__main__":
    main()
