#!/usr/bin/env python3
"""Decode indexed JA2 STCI/ETRLE sprites and build contact sheets.

Diagnostic-only tool for A3 rural-art selection. It never mutates game assets.
"""
from __future__ import annotations
import argparse
import json
import math
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

STCI_INDEXED = 0x0008
STCI_ETRLE_COMPRESSED = 0x0020
HEADER_SIZE = 64
SUBIMAGE_SIZE = 16

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def i16(b, o): return struct.unpack_from("<h", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]

def decode_sti(path: Path):
    b = path.read_bytes()
    if len(b) < HEADER_SIZE or b[:4] != b"STCI":
        raise ValueError(f"{path}: not STCI")
    stored = u32(b, 8)
    transparent = u32(b, 12)
    flags = u32(b, 16)
    image_h = u16(b, 20)
    image_w = u16(b, 22)
    ncolors = u32(b, 24)
    nsub = u16(b, 28)
    depth = b[44]
    app_size = u32(b, 45)
    if not (flags & STCI_INDEXED):
        raise ValueError(f"{path}: RGB STI is not handled by this audit")
    if ncolors <= 0 or ncolors > 256:
        raise ValueError(f"{path}: unexpected palette count {ncolors}")
    palette_off = HEADER_SIZE
    palette_end = palette_off + ncolors * 3
    if palette_end > len(b):
        raise ValueError(f"{path}: truncated palette")
    palette = [tuple(b[palette_off+i*3:palette_off+i*3+3]) for i in range(ncolors)]
    objects = []
    object_dir_off = palette_end
    if flags & STCI_ETRLE_COMPRESSED:
        if nsub == 0:
            raise ValueError(f"{path}: ETRLE with zero subimages")
        image_data_off = object_dir_off + nsub * SUBIMAGE_SIZE
        for i in range(nsub):
            o = object_dir_off + i * SUBIMAGE_SIZE
            data_off = u32(b, o)
            data_len = u32(b, o+4)
            ox = i16(b, o+8)
            oy = i16(b, o+10)
            h = u16(b, o+12)
            w = u16(b, o+14)
            objects.append((data_off, data_len, ox, oy, w, h))
    else:
        image_data_off = object_dir_off
        objects = [(0, stored, 0, 0, image_w, image_h)]

    if image_data_off + stored > len(b):
        raise ValueError(f"{path}: image payload exceeds file ({stored} bytes)")

    payload = b[image_data_off:image_data_off+stored]
    frames = []
    meta = []
    for idx, (data_off, data_len, ox, oy, w, h) in enumerate(objects):
        if data_off + data_len > len(payload):
            raise ValueError(f"{path}: frame {idx+1} payload outside image section")
        src = payload[data_off:data_off+data_len]
        rgba = bytearray(w*h*4)
        if flags & STCI_ETRLE_COMPRESSED:
            sp = 0
            dp = 0
            total = w*h
            while dp < total and sp < len(src):
                code = src[sp]; sp += 1
                count = code & 0x7F
                if count == 0:
                    # ETRLE scanline marker. Pixel position is already correct
                    # because runs account for the complete line.
                    continue
                if code & 0x80:
                    dp += count
                else:
                    if sp + count > len(src) or dp + count > total:
                        raise ValueError(f"{path}: corrupt frame {idx+1}")
                    for j in range(count):
                        pi = src[sp+j]
                        r,g,bl = palette[pi] if pi < len(palette) else (255,0,255)
                        q = (dp+j)*4
                        rgba[q:q+4] = bytes((r,g,bl,255))
                    sp += count
                    dp += count
            if dp < total:
                raise ValueError(f"{path}: frame {idx+1} decoded {dp}/{total} pixels")
        else:
            for p, pi in enumerate(src[:w*h]):
                r,g,bl = palette[pi] if pi < len(palette) else (255,0,255)
                a = 0 if pi == transparent else 255
                q = p*4
                rgba[q:q+4] = bytes((r,g,bl,a))
        frames.append(Image.frombytes("RGBA",(w,h),bytes(rgba)))
        meta.append(dict(frame=idx+1,width=w,height=h,offset_x=ox,offset_y=oy,
                         data_bytes=data_len))
    return frames, meta, dict(flags=flags, depth=depth, colors=ncolors,
                              subimages=nsub, stored_bytes=stored,
                              app_bytes=app_size, canvas=[image_w,image_h])

def contact_sheet(frames, meta, title: str):
    maxw=max(im.width for im in frames); maxh=max(im.height for im in frames)
    cellw=max(180,maxw+40); cellh=max(150,maxh+54)
    cols=4; rows=math.ceil(len(frames)/cols)
    sheet=Image.new("RGBA",(cellw*cols,cellh*rows),(34,34,34,255))
    d=ImageDraw.Draw(sheet); font=ImageFont.load_default()
    for i,(im,m) in enumerate(zip(frames,meta)):
        cx=(i%cols)*cellw; cy=(i//cols)*cellh
        d.rectangle((cx,cy,cx+cellw-1,cy+cellh-1),outline=(90,90,90,255))
        d.text((cx+5,cy+5),f"#{i+1} {im.width}x{im.height} off {m['offset_x']},{m['offset_y']}",
               fill=(255,255,255,255),font=font)
        x=cx+(cellw-im.width)//2
        y=cy+30+(cellh-34-im.height)//2
        sheet.alpha_composite(im,(x,y))
    return sheet

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True,type=Path)
    ap.add_argument("--out",required=True,type=Path)
    ap.add_argument("files",nargs="+")
    ns=ap.parse_args()
    ns.out.mkdir(parents=True,exist_ok=True)
    summary=[]
    for rel in ns.files:
        p=ns.root/rel
        frames,meta,hdr=decode_sti(p)
        safe=rel.replace("/","__").replace("\\","__")
        stem=Path(safe).stem
        contact_sheet(frames,meta,rel).save(ns.out/f"{stem}.png")
        (ns.out/f"{stem}.json").write_text(json.dumps({"file":rel,"header":hdr,"frames":meta},indent=2))
        summary.append({"file":rel,"frames":len(frames),**hdr})
        print(f"decoded {rel}: {len(frames)} frames")
    (ns.out/"summary.json").write_text(json.dumps(summary,indent=2))

if __name__=="__main__":
    main()
