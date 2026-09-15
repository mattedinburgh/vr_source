from __future__ import annotations
import argparse, csv, hashlib, json, re, struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from PIL import Image, ImageDraw

LAYERS=("land","object","struct","shadow","roof","onroof")
SEM={
 "empty":(18,18,18),"terrain":(177,145,88),"road":(126,111,91),
 "water":(74,113,135),"floor":(155,128,102),"wall":(182,105,78),
 "door":(125,77,55),"window":(79,133,139),"roof":(105,85,75),
 "fence":(126,107,77),"vegetation":(90,111,71),"prop":(128,111,91),
 "shadow":(55,55,55),"other":(145,126,105)
}

@dataclass(frozen=True)
class Header:
    major:float; minor:int; rows:int; cols:int; flags:int; tileset:int; soldier_size:int; header_bytes:int

@dataclass(frozen=True)
class Node:
    grid:int; row:int; col:int; room:int; layer:str; ordinal:int
    tile_type:int; subindex:int; tile_type_name:str; surface_file:str
    @property
    def key(self): return f"{self.layer}:{self.tile_type}:{self.subindex}"

def u8(b,o): return (b[o],o+1)
def u16(b,o): return (struct.unpack_from("<H",b,o)[0],o+2)
def i16(b,o): return (struct.unpack_from("<h",b,o)[0],o+2)
def u32(b,o): return (struct.unpack_from("<I",b,o)[0],o+4)
def i32(b,o): return (struct.unpack_from("<i",b,o)[0],o+4)

def header(raw):
    major=struct.unpack_from("<f",raw,0)[0]; o=4; minor=0
    if major>=4: minor,o=u8(raw,o)
    if major<6: raise ValueError(f"legacy map {major}/{minor} not supported by this fail-closed extractor")
    rows,o=i32(raw,o); cols,o=i32(raw,o); flags,o=u32(raw,o); ts,o=i32(raw,o); ss,o=u32(raw,o)
    if not(1<=rows<=1024 and 1<=cols<=1024): raise ValueError(f"bad dimensions {rows}x{cols}")
    return Header(major,minor,rows,cols,flags,ts,ss,o)

def names_from_tiledat(p):
    t=p.read_text(encoding="utf-8",errors="strict")
    m=re.search(r'STR\s+gTileSurfaceName\s*\[\s*NUMBEROFTILETYPES\s*\]\s*=\s*\{(.*?)\n\};',t,re.S)
    if not m: raise ValueError("gTileSurfaceName not found")
    return re.findall(r'"([^"]*)"',m.group(1))

def cstr(x): return x.split(b"\0",1)[0].decode("latin-1",errors="replace")
def ja2set(p):
    b=p.read_bytes(); o=0; n,o=u8(b,o); nf,o=u32(b,o); out=[]
    for idx in range(n):
        name=cstr(b[o:o+32]); o+=32; ambient,o=u8(b,o); fs=[]
        for _ in range(nf): fs.append(cstr(b[o:o+32])); o+=32
        out.append({"index":idx,"name":name,"ambient":ambient,"files":fs})
    return out,nf

def semantic(layer,n,f):
    s=(n+" "+f).upper()
    if layer=="shadow": return "shadow"
    if layer in ("roof","onroof") or "ROOF" in s: return "roof"
    if "WATER" in s: return "water"
    if "ROAD" in s: return "road"
    if "FLOOR" in s: return "floor"
    if "DOOR" in s: return "door"
    if "WINDOW" in s: return "window"
    if "WALL" in s or "BUILD_" in s: return "wall"
    if "FENCE" in s: return "fence"
    if any(q in s for q in ("TREE","BUSH","WEED","GRASS","PLANT","PALM","CROP","OSTRUCT","FULLSTRUCT")): return "vegetation"
    if layer=="land" or any(q in s for q in ("TEXTURE","DIRT","SAND","GROUND","TERRAIN","CLIFF")): return "terrain"
    if layer in ("object","struct"): return "prop"
    return "other"

def color(k):
    h=hashlib.sha256(k.encode()).digest()
    return tuple(48+x%176 for x in h[:3])

def parse_map(p,type_names,effective_files):
    b=p.read_bytes(); h=header(b); n=h.rows*h.cols; o=h.header_bytes
    heights=[]
    for _ in range(n): x,o=i16(b,o); heights.append(x)
    counts=[]
    for _ in range(n):
        a,o=u8(b,o); c,o=u8(b,o); d,o=u8(b,o); e,o=u8(b,o)
        counts.append({"land":a&15,"object":c&15,"struct":c>>4,"shadow":d&15,"roof":d>>4,"onroof":e&15})
    raw=[]
    for g in range(n):
        if counts[g]["land"]==0: _,o=u8(b,o); _,o=u8(b,o)
        else:
            for j in range(counts[g]["land"]):
                t,o=u8(b,o); s,o=u8(b,o); raw.append((g,"land",j,t,s))
    for g in range(n):
        for j in range(counts[g]["object"]):
            t,o=u8(b,o); s,o=u16(b,o); raw.append((g,"object",j,t,s))
    for layer in ("struct","shadow","roof","onroof"):
        for g in range(n):
            for j in range(counts[g][layer]):
                t,o=u8(b,o); s,o=u8(b,o); raw.append((g,layer,j,t,s))
    rooms=[]
    for _ in range(n):
        if h.minor<29: x,o=u8(b,o)
        else: x,o=u16(b,o)
        rooms.append(x)
    nodes=[]
    for g,l,j,t,s in raw:
        r,c=divmod(g,h.cols)
        tn=type_names[t] if t<len(type_names) else f"TYPE_{t}"
        sf=effective_files[t] if t<len(effective_files) else ""
        nodes.append(Node(g,r,c,rooms[g],l,j,t,s,tn,sf))
    return h,heights,rooms,nodes,o

def diamond(row,col,ox,oy,hw,hh):
    cx=ox+(col-row)*hw; cy=oy+(col+row)*hh
    return [(cx,cy-hh),(cx+hw,cy),(cx,cy+hh),(cx-hw,cy)]

def draw_map(path,h,bygrid,mode,palette):
    hw,hh=6,3; W=(h.rows+h.cols)*hw+hw*4; H=(h.rows+h.cols)*hh+hh*4
    ox=h.rows*hw+hw*2; oy=hh*2
    im=Image.new("RGB",(W,H),SEM["empty"]); dr=ImageDraw.Draw(im)
    precedence={"land":0,"shadow":1,"object":2,"struct":3,"onroof":4,"roof":5}
    for g,xs in bygrid.items():
        r,c=divmod(g,h.cols)
        if mode=="ground_eq": cand=[x for x in xs if x.layer=="land"]
        elif mode=="struct_eq": cand=[x for x in xs if x.layer in ("object","struct")]
        elif mode=="roof_eq": cand=[x for x in xs if x.layer in ("roof","onroof")]
        else:
            cand=[x for x in xs if not(mode=="roof_off" and x.layer in ("roof","onroof"))]
        if not cand: continue
        n=max(cand,key=lambda x:(precedence[x.layer],x.ordinal))
        colr=palette[n.key] if mode.endswith("_eq") else SEM[semantic(n.layer,n.tile_type_name,n.surface_file)]
        dr.polygon(diamond(r,c,ox,oy,hw,hh),fill=colr)
    im.save(path)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--map",required=True,type=Path)
    ap.add_argument("--ja2set",required=True,type=Path)
    ap.add_argument("--tiledat",required=True,type=Path)
    ap.add_argument("--out",required=True,type=Path)
    ap.add_argument("--sector")
    ns=ap.parse_args(); ns.out.mkdir(parents=True,exist_ok=True)
    tnames=names_from_tiledat(ns.tiledat); sets,nf=ja2set(ns.ja2set); h0=header(ns.map.read_bytes())
    if not(0<=h0.tileset<len(sets)): raise SystemExit(f"tileset {h0.tileset} outside JA2SET")
    active=sets[h0.tileset]; generic=sets[0]
    effective=[(active["files"][i] if i<len(active["files"]) and active["files"][i] else generic["files"][i] if i<len(generic["files"]) else "") for i in range(max(nf,len(tnames)))]
    h,heights,rooms,nodes,prefix=parse_map(ns.map,tnames,effective)
    sector=ns.sector or ns.map.stem
    classes=defaultdict(list); bygrid=defaultdict(list)
    for n in nodes: classes[n.key].append(n); bygrid[n.grid].append(n)
    pal={k:color(k) for k in classes}

    with (ns.out/f"{sector}_scene_nodes.csv").open("w",newline="",encoding="utf-8") as f:
        fields=["grid","row","col","room","layer","ordinal","tile_type","tile_type_name","subindex","surface_file","semantic","equivalence_key"]
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader()
        for n in sorted(nodes,key=lambda x:(x.grid,LAYERS.index(x.layer),x.ordinal)):
            w.writerow({"grid":n.grid,"row":n.row,"col":n.col,"room":n.room,"layer":n.layer,"ordinal":n.ordinal,"tile_type":n.tile_type,"tile_type_name":n.tile_type_name,"subindex":n.subindex,"surface_file":n.surface_file,"semantic":semantic(n.layer,n.tile_type_name,n.surface_file),"equivalence_key":n.key})

    with (ns.out/f"{sector}_frame_equivalence.csv").open("w",newline="",encoding="utf-8") as f:
        fields=["equivalence_key","layer","tile_type","tile_type_name","subindex","surface_file","semantic","occurrences","rooms","example_grids"]
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader()
        for k,xs in sorted(classes.items(),key=lambda kv:(-len(kv[1]),kv[0])):
            n=xs[0]
            w.writerow({"equivalence_key":k,"layer":n.layer,"tile_type":n.tile_type,"tile_type_name":n.tile_type_name,"subindex":n.subindex,"surface_file":n.surface_file,"semantic":semantic(n.layer,n.tile_type_name,n.surface_file),"occurrences":len(xs),"rooms":" ".join(map(str,sorted({x.room for x in xs if x.room}))),"example_grids":" ".join(map(str,[x.grid for x in xs[:12]]))})

    draw_map(ns.out/f"{sector}_roof_on_semantic.png",h,bygrid,"roof_on",pal)
    draw_map(ns.out/f"{sector}_roof_off_semantic.png",h,bygrid,"roof_off",pal)
    draw_map(ns.out/f"{sector}_ground_equivalence.png",h,bygrid,"ground_eq",pal)
    draw_map(ns.out/f"{sector}_struct_equivalence.png",h,bygrid,"struct_eq",pal)
    draw_map(ns.out/f"{sector}_roof_equivalence.png",h,bygrid,"roof_eq",pal)

    semcount=Counter(semantic(n.layer,n.tile_type_name,n.surface_file) for n in nodes)
    layercount=Counter(n.layer for n in nodes)
    manifest={
      "sector":sector,
      "source_map":str(ns.map),
      "map_sha256":hashlib.sha256(ns.map.read_bytes()).hexdigest(),
      "map_contract":{"major":h.major,"minor":h.minor,"rows":h.rows,"cols":h.cols,"flags":f"0x{h.flags:08X}","tileset_id":h.tileset,"tileset_name":active["name"],"prefix_bytes_parsed":prefix},
      "new_engine_contract":{
        "authority_branch":"engine/vhd-modernization-2026",
        "native_scales":[2,4],
        "native_visual_formats":["B1TC","JPC","PNG"],
        "hard_constraints":[
          "native frame count equals canonical STI frame count",
          "native frame width/height equals canonical width/height multiplied by VHD scale",
          "native frame offsets equal canonical offsets multiplied by VHD scale",
          "gameplay/app metadata is canonicalized from the STI",
          "LEVELNODE retains tile-database usIndex/frame identity; no per-node visual-art override exists"
        ],
        "not_a_hard_engine_constraint":[
          "legacy alpha footprint does not need to be byte-identical; gameplay geometry remains canonical STI/JSD",
          "legacy indexed colour/palette"
        ]
      },
      "generation_rule":"All occurrences of one layer:tile_type:subindex equivalence key are one shared visual variable. Whole-scene generation must reuse that variable identically.",
      "nodes":len(nodes),"equivalence_classes":len(classes),
      "layer_counts":dict(layercount),"semantic_counts":dict(semcount),
      "largest_equivalence_classes":[{"key":k,"occurrences":len(xs),"surface_file":xs[0].surface_file,"semantic":semantic(xs[0].layer,xs[0].tile_type_name,xs[0].surface_file)} for k,xs in sorted(classes.items(),key=lambda kv:-len(kv[1]))[:40]]
    }
    (ns.out/f"{sector}_constraint_manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    (ns.out/f"{sector}_conditioning_palette.json").write_text(json.dumps({k:pal[k] for k in sorted(pal)},indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"sector":sector,"nodes":len(nodes),"classes":len(classes),"layers":dict(layercount),"semantics":dict(semcount),"tileset":active["name"]},indent=2))

if __name__=="__main__": main()

[executed on device: MSI (e4de0d4a-2679-4f35-acaf-b6552e7ec980)]