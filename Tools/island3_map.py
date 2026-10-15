"""Bake Mangrove Hollow's chart from the authored terrain rectangles.

Centre (-14000,13000), half extent 6000, beach half 4500. +X is up and
+Y is right, exactly WrecktideMap::WorldToUV. Dark water, mud, banks,
stilt roofs and altar stone. /Game/UI is already always cooked.
Headless stdlib PNG writer; optional verify checks the texture and clamp modes.
"""
import math
from pathlib import Path
import struct
import sys
import zlib
import unreal

SIZE = 2048
DEST = "/Game/UI/Map"
NAME = "T_Island3Map"
PATH = DEST + "/" + NAME
CX, CY, HALF = -14000.0, 13000.0, 6000.0


def log(msg):
    unreal.log_warning("WRECKTIDE-ISLE3MAP " + msg)


def need(ok, msg):
    if not ok:
        unreal.log_error("WRECKTIDE-ISLE3MAP RESULT=FAIL " + msg)
        raise SystemExit(1)


def in_box(x, y, cx, cy, hx, hy):
    return abs(x - cx) <= hx and abs(y - cy) <= hy


def plate():
    raw = bytearray()
    for py in range(SIZE):
        x = CX + HALF - (py + 0.5) * 2 * HALF / SIZE
        raw.append(0)
        for px in range(SIZE):
            y = CY - HALF + (px + 0.5) * 2 * HALF / SIZE
            edge = max(abs(x - CX), abs(y - CY))
            ripple = int(3 * math.sin(x / 120 + y / 340))
            if edge > 4500:
                t = max(0, 1 - (edge - 4500) / 650)
                col = (int(19 + 25*t), int(43 + 29*t), int(44 + 25*t))
            elif edge > 3000:
                col = (139, 133, 95) if edge < 4450 else (166, 164, 123)
            elif in_box(x, y, -11900, 12100, 900, 900):
                seam = int(y / 80) % 2
                col = (110 + seam*9, 78 + seam*6, 43 + seam*4)
                for hx, hy in [(-12400,12450), (-11450,12500), (-12400,11850)]:
                    if in_box(x, y, hx, hy, 285, 285):
                        col = (160, 117, 62)
            elif in_box(x, y, -16100, 14400, 600, 600):
                col = (113, 142, 130) if in_box(x, y, -16100, 14400, 400, 400) else (71, 94, 80)
            elif in_box(x, y, CX, CY, 1300, 1300):
                wet = 0.5 + 0.5 * math.sin(x/430) * math.cos(y/270)
                col = (int(49-12*wet), int(62+9*wet), int(43+21*wet))
                for hx, hy in [(-14700,12400), (-13200,13550), (-14700,13700)]:
                    if in_box(x,y,hx,hy,240,210):
                        col = (88, 84, 58)
            else:
                col = (83, 103, 60)
            raw.extend((max(0,min(255,col[0]+ripple)), max(0,min(255,col[1]+ripple)),
                        max(0,min(255,col[2]+ripple)), 255))
    return raw


def main():
    eal = unreal.EditorAssetLibrary
    if "verify" not in sys.argv:
        out = Path(unreal.Paths.project_content_dir()).resolve() / "UI" / "Map" / "island3_map.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
        out.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE,SIZE,8,6,0,0,0))
                        + chunk(b"IDAT", zlib.compress(plate(), 6)) + chunk(b"IEND", b""))
        task = unreal.AssetImportTask()
        for key, val in {"filename": str(out), "destination_path": DEST, "destination_name": NAME,
                         "replace_existing": True, "automated": True, "save": True}.items():
            task.set_editor_property(key, val)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        asset = eal.load_asset(PATH)
        need(asset, "import")
        asset.set_editor_property("srgb", True)
        asset.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        asset.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        need(eal.save_loaded_asset(asset), "save")
    asset = eal.load_asset(PATH)
    need(asset and asset.blueprint_get_size_x() == SIZE and asset.blueprint_get_size_y() == SIZE, "plate dimensions")
    need(asset.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP
         and asset.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP, "clamp")
    log("RESULT=OK centre=(-14000,13000) half=6000 beachHalf=4500 plate=2048x2048 clamp=1")


main()
