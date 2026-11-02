#!/usr/bin/env python3
"""Convert one level of the main port (read-only) into DS data: nitrofiles/lNN.bin (geometry display lists,
visibility, BSP collision, objects) and nitrofiles/lNN.tex (VRAM texture and palette images)."""
import json, struct, sys, math, os, collections
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import convert_game
MUSIC = []
SOUNDS = {}
SRC = os.path.join(os.path.dirname(__file__), '..', '..', 'RCSPOOK_NEW')
OUT = os.path.join(os.path.dirname(__file__), '..', 'nitrofiles')
CELL = 512; MAX_EDGE = 1000; TEX_MAX = 64
FX = 4096

# ---------- GX command packing ----------
CMD = dict(MTX_RESTORE=0x14, MTX_SCALE=0x1B, MTX_TRANS=0x1C, COLOR=0x20, TEXCOORD=0x22, VTX10=0x24, POLYFMT=0x29, TEXIMAGE=0x2A, PLTT=0x2B, BEGIN=0x40, END=0x41)
class GX:
    def __init__(s): s.cmds = []
    def add(s, name, *params): s.cmds.append((CMD[name], params))
    def words(s):
        out = []; i = 0; c = s.cmds
        while i < len(c):
            group = c[i:i+4]; i += 4
            head = 0
            for k, (cmd, _) in enumerate(group): head |= cmd << (8*k)
            out.append(head)
            for _, params in group: out.extend(x & 0xFFFFFFFF for x in params)
        return out
def texcoord(u, v): return (int(u) & 0xFFFF) | ((int(v) & 0xFFFF) << 16)
def vtx10(x, y, z):
    assert -512 <= x < 512 and -512 <= y < 512 and -512 <= z < 512, (x, y, z)
    return (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20)
def rgb15(c):
    r, g, b = [max(0, min(31, int(round(x*31)))) for x in c]
    return r | (g << 5) | (b << 10)

# ---------- textures ----------
def log2size(n): return {8:0,16:1,32:2,64:3,128:4,256:5,512:6,1024:7}[n]
class TexBank:
    """Packs 16-colour textures and palettes; colour 0 is transparent when the texture has a colour key."""
    def __init__(s, vram=0, pal=0): s.vram = bytearray(); s.pal = bytearray(); s.base = vram; s.palbase = pal; s.table = {}
    def add_256(s, key, path, keyed, size):
        # 256 colours for RedCat, so he stays recognizable.
        if key in s.table: return s.table[key]
        im = Image.open(path).convert('RGBA').resize((size, size), Image.BOX); a = np.array(im)
        mask = (a[:, :, 3] < 128) if keyed else np.zeros((size, size), bool)
        colors = 255 if keyed else 256
        q = Image.fromarray(a[:, :, :3], 'RGB').quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE)
        idx = np.array(q); palette = q.getpalette()[:colors*3]; n_ = len(palette)//3
        if keyed: idx = idx+1; idx[mask] = 0
        pal = [0]*(1 if keyed else 0)+[rgb15([palette[i*3]/255, palette[i*3+1]/255, palette[i*3+2]/255]) for i in range(n_)]
        pal += [0]*(256-len(pal))
        off = len(s.vram)+s.base; s.vram += idx.astype(np.uint8).tobytes()
        poff = len(s.pal)+s.palbase; s.pal += struct.pack('<256H', *pal)
        param = ((off >> 3) & 0xFFFF) | (1 << 16) | (1 << 17) | (log2size(size) << 20) | (log2size(size) << 23) | (4 << 26) | ((1 if keyed else 0) << 29)
        info = dict(param=param, pltt=poff >> 4, w=size, h=size, keyed=keyed)
        s.table[key] = info
        return info

    def add(s, key, path, keyed, maxsize=TEX_MAX):
        if key in s.table: return s.table[key]
        im = Image.open(path).convert('RGBA')
        w, h = im.size
        tw = min(maxsize, max(8, 1 << int(math.ceil(math.log2(w))))); th = min(maxsize, max(8, 1 << int(math.ceil(math.log2(h)))))
        im = im.resize((tw, th), Image.BOX if tw < w or th < h else Image.NEAREST)
        a = np.array(im)
        mask = (a[:, :, 3] < 128) if keyed else np.zeros((th, tw), bool)
        rgb = Image.fromarray(a[:, :, :3], 'RGB')
        colors = 15 if keyed else 16
        q = rgb.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE)
        idx = np.array(q); palette = q.getpalette()[:colors*3]
        n = len(palette)//3
        if keyed: idx = idx + 1; idx[mask] = 0
        pal = [0]*(1 if keyed else 0) + [rgb15([palette[i*3]/255, palette[i*3+1]/255, palette[i*3+2]/255]) for i in range(n)]
        pal += [0]*(16-len(pal))
        px = bytearray((int(idx[y, x]) & 15) | ((int(idx[y, x+1]) & 15) << 4) for y in range(th) for x in range(0, tw, 2))
        off = len(s.vram) + s.base; s.vram += px
        poff = len(s.pal) + s.palbase; s.pal += struct.pack('<16H', *pal)
        param = ((off >> 3) & 0xFFFF) | (1 << 16) | (1 << 17) | (log2size(tw) << 20) | (log2size(th) << 23) | (3 << 26) | ((1 if keyed else 0) << 29)
        info = dict(param=param, pltt=poff >> 4, w=tw, h=th, keyed=keyed, avg=tuple(np.array(rgb).reshape(-1, 3).mean(0)))
        s.table[key] = info
        return info

    def add_alpha(s, key, path, size=32):
        # A5I3 for soft glows: 5 bits of alpha, 3 bits of colour.
        if key in s.table: return s.table[key]
        im = Image.open(path).convert('RGBA').resize((size, size), Image.BOX); a = np.array(im)
        q = Image.fromarray(a[:, :, :3], 'RGB').quantize(colors=8, method=Image.MEDIANCUT, dither=Image.NONE)
        idx = np.array(q); palette = q.getpalette()[:24]; palette += [0]*(24-len(palette))
        px = (idx.astype(np.uint8) & 7) | ((a[:, :, 3] >> 3).astype(np.uint8) << 3)
        off = len(s.vram)+s.base; s.vram += px.tobytes()
        poff = len(s.pal)+s.palbase
        s.pal += struct.pack('<8H', *[rgb15([palette[i*3]/255, palette[i*3+1]/255, palette[i*3+2]/255]) for i in range(8)]) + b'\0'*16
        info = dict(param=((off >> 3) & 0xFFFF) | (log2size(size) << 20) | (log2size(size) << 23) | (6 << 26), pltt=poff >> 4, w=size, h=size)
        s.table[key] = info
        return info

POLY_BASE = (31 << 16)  # alpha 31, polygon id 0
def polyfmt(fog=True, cull_back=True, alpha=31, id=0):
    return (alpha << 16) | (id << 24) | ((2 if cull_back else 3) << 6) | ((1 << 15) if fog else 0) | (1 << 11)

def main(level_id, index):
    base = f'{SRC}/data/levels/{level_id}/'
    d = json.load(open(base + 'level.json')); vis = json.load(open(f'{SRC}/data/visibility/{level_id}.json')); pvs = np.fromfile(f'{SRC}/data/visibility/{level_id}.bin', dtype=np.uint8)
    verts = np.fromfile(base + 'mesh.bin', dtype='<f4').reshape(-1, 11)
    lmuv = np.fromfile(base + 'lightmap-uv.bin', dtype='<f4').reshape(-1, 2)
    lm = np.array(Image.open(base + 'lightmap.png').convert('RGB'), dtype=np.float32) / 255
    LH, LW = lm.shape[:2]
    def sample_lm(u, v):
        x = u*LW - .5; y = v*LH - .5; x0 = int(math.floor(x)); y0 = int(math.floor(y)); fx = x-x0; fy = y-y0
        g = lambda a, b: lm[max(0, min(LH-1, b)), max(0, min(LW-1, a))]
        return (g(x0, y0)*(1-fx)+g(x0+1, y0)*fx)*(1-fy) + (g(x0, y0+1)*(1-fx)+g(x0+1, y0+1)*fx)*fy
    names = {e['%name%']: int(e['Model']) for e in d['entities'] if e.get('classname') == '%Model%' and 'Model' in e}
    # models used by triggers only are invisible
    used_by = collections.defaultdict(set)
    for e in d['entities']:
        m = e.get('Model')
        if m in names: used_by[names[m]].add(e.get('classname'))
    trigger_models = {m for m, c in used_by.items() if c <= {'Trigger', 'CameraTrigger'} and c}
    damaging = {names[e['Model']] for e in d['entities'] if e.get('classname') == 'Trigger' and float(e.get('DamagePerSecond') or 0) > 0 and e.get('Model') in names}
    def liquid_group(g):
        return g['alpha'] > 0 and (g['alpha'] < 1 or (g['alpha'] == 1 and g['model'] in damaging)) and d['textures'][g['texture']]['name'].lower().startswith('air_wtr')
    liquid_models = {g['model'] for g in d['groups'] if liquid_group(g)}
    bank = TexBank()
    def tex_info(i):
        t = d['textures'][i]
        return bank.add(i, base + t['file'], bool(t.get('colorKey')) and not (level_id == 'lvl02a' and t['name'] == 'hek_door'))
    spans = vis['faceSpans']; span_face = {}
    leaf_of_face = collections.defaultdict(list)
    for li, leaf in enumerate(vis['leaves']):
        cl, area, first, count = leaf
        for f in vis['leafFaces'][first:first+count]: leaf_of_face[f].append(cl)
    # polygons per (model, cell)
    faces_by_vertex = sorted(spans, key=lambda s: s[1])
    fi = 0
    world = collections.defaultdict(list); models = collections.defaultdict(list)
    cell_clusters = collections.defaultdict(set)
    tris_in = 0; polys_out = 0; light = []
    def split(tri):
        # tri: list of 3 vertex tuples (x,y,z,u,v,r,g,b); splits every edge longer than MAX_EDGE at its midpoint
        best = -1; bi = -1
        for i in range(3):
            a, b = tri[i], tri[(i+1) % 3]
            l = math.dist(a[:3], b[:3])
            if l > best: best = l; bi = i
        if best <= MAX_EDGE: return [tri]
        a, b, c = tri[bi], tri[(bi+1) % 3], tri[(bi+2) % 3]
        m = tuple((p+q)/2 for p, q in zip(a, b))
        return split([a, m, c]) + split([m, b, c])
    for g in d['groups']:
        if g['alpha'] <= 0: continue
        flags = g['flags']; sky = bool(flags & 4); full = bool(flags & 2); gouraud = bool(flags & 32)
        if sky: continue
        if g['model'] in trigger_models and not liquid_group(g): continue
        info = tex_info(g['texture'])
        alpha5 = max(1, min(31, round(g['alpha']*31)))
        end = g['start'] + g['count']
        # walk the face spans of this group
        pos = g['start']
        while pos < end:
            while fi < len(faces_by_vertex) and faces_by_vertex[fi][1] + faces_by_vertex[fi][2] <= pos: fi += 1
            if fi < len(faces_by_vertex) and faces_by_vertex[fi][1] <= pos:
                face, fs, fc = faces_by_vertex[fi]; count = min(end, fs+fc) - pos
            else:
                face, count = -1, end-pos
            tris = []
            for k in range(pos, pos+count, 3):
                t = []
                for j in range(3):
                    v = verts[k+j]
                    if full: col = (1, 1, 1)
                    elif gouraud: col = tuple(v[8:11])
                    else: col = tuple(sample_lm(lmuv[k+j][0], lmuv[k+j][1]) ** (1/2.2))
                    t.append((float(v[0]), float(v[1]), float(v[2]), float(v[6]), float(v[7]), float(col[0]), float(col[1]), float(col[2])))
                tris.append(t)
            tris_in += len(tris)
            # pair fan triangles into quads, split long ones
            polys = []; i = 0
            while i < len(tris):
                t = tris[i]
                if i+1 < len(tris) and tris[i+1][0] == t[0] and tris[i+1][1] == t[2]:
                    q = [t[0], t[1], t[2], tris[i+1][2]]
                    if max(math.dist(q[a][:3], q[(a+1) % 4][:3]) for a in range(4)) <= MAX_EDGE and math.dist(q[0][:3], q[2][:3]) <= MAX_EDGE and math.dist(q[1][:3], q[3][:3]) <= MAX_EDGE:
                        polys.append(q); i += 2; continue
                polys.extend(split(t)); i += 1
            for p in polys:
                cx, cy, cz = [sum(v[a] for v in p)/len(p) for a in range(3)]
                if not gouraud and not full: light.append((cx, cy, cz, sum(v[5] for v in p)/len(p), sum(v[6] for v in p)/len(p), sum(v[7] for v in p)/len(p)))
                if g['model'] == 0:
                    key = (int(cx//CELL), int(cy//CELL), int(cz//CELL))
                    world[key].append((g['texture'], flags | (alpha5 << 24), p))
                    if face >= 0: cell_clusters[key].update(leaf_of_face.get(face, ()))
                else:
                    models[g['model']].append((g['texture'], flags | (alpha5 << 24), p))
            polys_out += len(polys)
            pos += count
    # surface decals (the UFO's crater, lily pads) are baked into the world, placed as placeDecal/decalGeometry in the main port
    decal_tris = []
    for g in d['groups']:
        if g['alpha'] > 0 and not g['flags'] & 4: decal_tris.append(verts[g['start']:g['start']+g['count'], :3])
    decal_tris = np.concatenate(decal_tris).reshape(-1, 3, 3) if decal_tris else np.zeros((0, 3, 3))
    def ray_hit(o, dr):
        a = decal_tris[:, 0]; e1 = decal_tris[:, 1]-a; e2 = decal_tris[:, 2]-a
        h = np.cross(dr, e2); det = (e1*h).sum(1); ok = np.abs(det) > 1e-6
        inv = np.where(ok, 1/np.where(ok, det, 1), 0); t0 = o-a
        u = (t0*h).sum(1)*inv; q = np.cross(t0, e1); w = (q*dr).sum(1)*inv; t = (q*e2).sum(1)*inv
        hit = ok & (u >= 0) & (w >= 0) & (u+w <= 1) & (t > 0) & (t < 3200)
        if not hit.any(): return None
        i = np.where(hit)[0][np.argmin(t[hit])]
        n_ = np.cross(e1[i], e2[i]); n_ = n_/max(1e-9, np.linalg.norm(n_))
        if n_ @ dr > 0: n_ = -n_
        return float(t[i]), n_
    ndecals = 0
    for e in d['entities']:
        if e.get('classname') != 'EffectDecalEntity' or not float(e.get('IsInitiallyEnabled', 1)): continue
        name = f"{str(e.get('BitmapFileName','')).lower()[:-4]}-{str(e.get('BitmapAlphaFileName','')).lower()[:-4]}.png"
        path = f'{SRC}/assets/effects/{name}'
        if not os.path.exists(path): continue
        o = np.array([float(x) for x in e['Origin'].split()]); placement = int(float(e.get('OriginPlacement', 1)))
        dirs = {1: [(0, -1, 0)], 2: [(0, 1, 0)], 3: [(math.sin(i*math.pi/4), 0, math.cos(i*math.pi/4)) for i in range(8)]}.get(placement, [])
        hits = [(h, dr) for dr in dirs for h in [ray_hit(o, np.array(dr, float))] if h]
        if not hits: continue
        (t, nrm), dr = min(hits, key=lambda x: x[0][0])
        pos = o+np.array(dr)*t+nrm*2   # lifted by the vertex grid's 2 units, so it cannot z-fight with the surface
        ref = np.array([0, -1 if nrm[1] < 0 else 1, 0.])
        if (np.abs(nrm-ref) <= .1).all(): ref = np.array([0, 0, -1 if nrm[2] < 0 else 1.])
        right = np.cross(nrm, ref); right /= np.linalg.norm(right); up = np.cross(nrm, right); up /= np.linalg.norm(up)
        rot = float(e.get('Rotation', 0))*math.pi/6; c, s_ = math.cos(rot), math.sin(rot)
        w_, h_ = float(e.get('SizeWidth', 32)), float(e.get('SizeHeight', 32))
        tint = [max(0, min(1, float(x)/255)) for x in str(e.get('OverallColor', '255 255 255')).split()]
        corners = [(1, 1, 0, 1), (-1, 1, 1, 1), (-1, -1, 1, 0), (1, -1, 0, 0)]
        quad = []
        for x, z, u, v in corners:
            a_, b_ = (x*c+z*s_)*w_/2, (-x*s_+z*c)*h_/2
            q = pos+right*a_+up*b_
            quad.append((float(q[0]), float(q[1]), float(q[2]), float(u), float(v), *tint))
        d['textures'].append(dict(file=os.path.relpath(path, base), name=name, colorKey=True)); ti = len(d['textures'])-1
        for k, t_ in enumerate(d['textures'][:-1]):
            if t_.get('file') == d['textures'][-1]['file']: d['textures'].pop(); ti = k; break
        cx, cy, cz = pos
        world[(int(cx//CELL), int(cy//CELL), int(cz//CELL))].append((ti, 4 | (31 << 24), quad)); ndecals += 1; polys_out += 1
    if ndecals: print(f'  decals {ndecals}')
    # dense cells are split further, so the polygon budget can drop single small pieces
    for key in list(world.keys()):
        items = world[key]
        if len(items) <= 260: continue
        del world[key]; cl = cell_clusters.pop(key, set())
        for it in items:
            p = it[2]; c = [sum(v[a] for v in p)/len(p) for a in range(3)]
            k2 = key+(int((c[0]-key[0]*CELL)//(CELL//2)), int((c[1]-key[1]*CELL)//(CELL//2)), int((c[2]-key[2]*CELL)//(CELL//2)))
            world[k2].append(it); cell_clusters[k2] |= cl
    # ---------- build cell lists ----------
    def build_list(items, origin, prefix=None, step=2):
        gx = GX()
        if prefix: prefix(gx)
        ox, oy, oz = origin
        items.sort(key=lambda it: (d['textures'][it[0]].get('colorKey', False), it[0]))
        last = None
        polys = 0
        order = sorted(items, key=lambda it: (it[1] >> 24 < 31, it[0], it[1] >> 24, len(it[2])))
        i = 0
        cur_tex = None; cur_kind = None
        for tex, flags, p in order:
            info = tex_info(tex)
            if (tex, flags >> 24) != cur_tex:
                if cur_kind is not None: gx.add('END'); cur_kind = None
                gx.add('TEXIMAGE', info['param']); gx.add('PLTT', info['pltt'])
                gx.add('POLYFMT', polyfmt(fog=True, cull_back=True, alpha=flags >> 24, id=0 if flags >> 24 == 31 else 1)); cur_tex = (tex, flags >> 24)
            kind = 1 if len(p) == 4 else 0
            if kind != cur_kind:
                if cur_kind is not None: gx.add('END')
                gx.add('BEGIN', kind); cur_kind = kind
            us = [v[3]*info['w'] for v in p]; vs = [v[4]*info['h'] for v in p]
            su = math.floor(min(us)/info['w'])*info['w']; sv = math.floor(min(vs)/info['h'])*info['h']
            # three.js front faces are counter clockwise, as on the DS after the Z flip free view matrix
            for v, tu, tv in zip(p, us, vs):
                gx.add('COLOR', rgb15(v[5:8]))
                gx.add('TEXCOORD', texcoord(max(-32768, min(32767, round((tu-su)*16))), max(-32768, min(32767, round((tv-sv)*16)))))
                gx.add('VTX10', vtx10((step*round(v[0]/step)-ox)//step, (step*round(v[1]/step)-oy)//step, (step*round(v[2]/step)-oz)//step))
            polys += 1
        if cur_kind is not None: gx.add('END')
        return gx.words(), polys
    lists = bytearray(); cells = []
    keys = sorted(world.keys())
    cell_index = {k: i for i, k in enumerate(keys)}
    for key in keys:
        items = world[key]
        xs = [round(v[0]) for _, _, p in items for v in p]; ys = [round(v[1]) for _, _, p in items for v in p]; zs = [round(v[2]) for _, _, p in items for v in p]
        origin = tuple(2*((min(a)+max(a))//4) for a in (xs, ys, zs))
        def prefix(gx, o=origin):
            gx.add('MTX_RESTORE', 1); gx.add('MTX_TRANS', o[0]*64, o[1]*64, o[2]*64); gx.add('MTX_SCALE', 8192, 8192, 8192)
        words, polys = build_list(items, origin, prefix)
        cells.append(struct.pack('<6hIHH', min(xs), min(ys), min(zs), max(xs), max(ys), max(zs), len(lists), polys, len(words)))
        lists += struct.pack(f'<{len(words)}I', *words)
    # brush model lists (world coordinates relative to the model centre)
    model_table = []
    colmodels = d['collision']['models']
    for mi in range(len(colmodels)):
        m = colmodels[mi]; items = models.get(mi)
        if items:
            xs = [round(v[0]) for _, _, p in items for v in p]; ys = [round(v[1]) for _, _, p in items for v in p]; zs = [round(v[2]) for _, _, p in items for v in p]
            origin = tuple(2*((min(a)+max(a))//4) for a in (xs, ys, zs))
            ext = max(max(a)-min(a) for a in (xs, ys, zs))
            step = 2 if ext < 2040 else 4 if ext < 4080 else 8
            origin = tuple(step*(((min(a)+max(a))//2)//step) for a in (xs, ys, zs))
            def prefix_m(gx, st=step): gx.add('MTX_SCALE', st*4096, st*4096, st*4096)
            words, polys = build_list(items, origin, prefix_m, step)
            model_table.append((len(lists), polys, len(words), origin))
            lists += struct.pack(f'<{len(words)}I', *words)
        else:
            model_table.append((0, 0, 0, (0, 0, 0)))
    # ---------- visibility ----------
    nclusters = len(vis['clusters']); ncells = len(keys); stride = (ncells+7)//8
    cells_of_cluster = np.zeros((nclusters, ncells), dtype=bool)
    for key, cl in cell_clusters.items():
        for c in cl:
            if c >= 0: cells_of_cluster[c, cell_index[key]] = True
    # cells without known faces stay always visible
    always = np.array([not cell_clusters[k] or all(c < 0 for c in cell_clusters[k]) for k in keys], dtype=bool)
    rowbytes = (nclusters+7)//8; rows = {}; cluster_row = []; rowdata = bytearray()
    for c in range(nclusters):
        off = vis['clusters'][c]
        if off < 0: mask = np.ones(ncells, dtype=bool)
        else:
            bits = np.unpackbits(pvs[off:off+rowbytes], bitorder='little')[:nclusters].astype(bool); bits[c] = True
            mask = cells_of_cluster[bits].any(axis=0) | always
        b = np.packbits(mask, bitorder='little').tobytes()
        if b not in rows: rows[b] = len(rows); rowdata += b
        cluster_row.append(rows[b])
    # ---------- collision ----------
    col = d['collision']
    planes = b''.join(struct.pack('<4i', *[int(round(x*(1 << 30))) for x in p[:3]], int(round(p[3]*FX))) for p in col['planes'])
    nodes = b''.join(struct.pack('<hhH', n[0], n[1], n[2]) for n in col['nodes'])
    clamp16 = lambda v: max(-32768, min(32767, int(math.floor(v) if v < 0 else math.ceil(v))))
    vleaves = vis['leaves']
    leaves = b''
    for i, l in enumerate(col['leaves']):
        mn = l.get('min', [-32768]*3); mx = l.get('max', [32767]*3)
        cl = vleaves[i][0] if i < len(vleaves) else -1; ar = vleaves[i][1] if i < len(vleaves) else 0
        leaves += struct.pack('<HHHh6h', (l['contents'] & 0xFF) | (((l['contents'] >> 16) & 0xFF) << 8), l.get('firstSide', 0) & 0xFFFF, l.get('numSides', 0), cl, *[clamp16(v-1) for v in mn], *[clamp16(v+1) for v in mx])
    sides = b''.join(struct.pack('<H', s[0] | ((1 if s[1] else 0) << 15)) for s in col['leafSides'])
    assert len(col['planes']) < 32768 and len(col['nodes']) < 32768 and len(col['leaves']) < 32768 and len(col['leafSides']) < 65536
    def has_liquid(m):
        pending = [m['root']]; seen = set()
        while pending:
            n_ = pending.pop()
            if n_ in seen: continue
            seen.add(n_)
            if n_ < 0:
                if col['leaves'][-n_-1]['contents'] & 0x70000: return True
            else:
                pending += col['nodes'][n_][:2]
        return False
    modelbin = b''
    for i, m in enumerate(col['models']):
        lo, polys, words, o = model_table[i]
        mflags = (1 if i in liquid_models else 0) | (2 if i and has_liquid(m) else 0)
        modelbin += struct.pack('<h6h3hIHHHH', m['root'], *[clamp16(v-1) for v in m['min']], *[clamp16(v+1) for v in m['max']], *o, lo, polys, words, mflags, 0)
    # ---------- sky ----------
    sky = d.get('sky') or {}
    skytex = []
    for t in sky.get('textures', [])[:6]:
        skytex.append(tex_info(t) if t >= 0 else None)
    fogc = (0, 0, 0)
    sk = [t['avg'] for t in skytex if t]
    if sk: fogc = tuple(sum(c[i] for c in sk)/len(sk)/255 for i in range(3))
    skybin = b''.join(struct.pack('<II', t['param'], t['pltt']) if t else struct.pack('<II', 0, 0) for t in (skytex+[None]*6)[:6])
    fx_dir = f'{os.path.abspath(SRC)}/assets/effects/'
    has_fairy = any(e.get('classname') == 'Fairy' for e in d['entities'])
    # same order as the SP_ enum in source/game.h; 32x32 A5I3, a kilobyte each
    assets = f'{SRC}/assets/'
    sprites = [bank.add_alpha('s:'+f, assets+f) if os.path.exists(assets+f) else None for f in convert_game.SPRITES]
    skybin += b''.join(struct.pack('<II', t['param'], t['pltt']) if t else struct.pack('<II', 0, 0) for t in sprites)
    # ---------- assemble ----------
    sp = d['spawn']
    point = lambda e: [float(x) for x in e['Origin'].split()]
    stops = [sp['position']] + [point(e) for e in d['entities'] if e.get('classname') == 'MovingEnemy']
    stopbin = struct.pack('<I', len(stops)) + b''.join(struct.pack('<3i', *[int(v*FX) for v in s]) for s in stops)
    sections = [bytes(lists), b''.join(cells), struct.pack(f'<{nclusters}H', *cluster_row), bytes(rowdata), planes, nodes, leaves, sides, modelbin, stopbin]
    head_size = 4*32
    offs = []; pos = head_size
    blob = bytearray()
    for s in sections:
        pad = (-pos) & 3; blob += b'\0'*pad; pos += pad
        offs.append(pos); blob += s; pos += len(s)
    sp = d['spawn']
    header = struct.pack('<16I', 0x52433253, ncells, nclusters, stride, len(col['planes']), len(col['nodes']), len(col['leaves']), len(col['leafSides']), len(col['models']), *offs[:7])
    header += struct.pack('<2I', offs[7], offs[8])
    header += struct.pack('<4i', *[int(sp['position'][i]*FX) for i in range(3)], int(float(sp.get('orientation', 0))))
    header += struct.pack('<II', rgb15(fogc), offs[9])
    header = header.ljust(head_size, b'\0')
    os.makedirs(OUT, exist_ok=True)
    convert_game.build_game(level_id, index, d, bank, light, os.path.abspath(SRC), OUT, MUSIC, SOUNDS)
    assert len(bank.vram) <= 384*1024, 'texture memory exceeded'
    open(f'{OUT}/l{index}.bin', 'wb').write(header + blob)
    open(f'{OUT}/l{index}.tex', 'wb').write(struct.pack('<2I', len(bank.vram), len(bank.pal)) + bytes(bank.vram) + bytes(bank.pal) + skybin)
    wpolys = [struct.unpack('<6hIHH', c)[7] for c in cells]
    print(level_id, f'tris {tris_in} -> polys {polys_out}; cells {ncells} (max {max(wpolys)} polys); lists {len(lists)//1024} KiB; vis rows {len(rows)} ({len(rowdata)//1024} KiB); '
          f'collision {(len(planes)+len(nodes)+len(leaves)+len(sides))//1024} KiB; textures {len(bank.table)} vram {len(bank.vram)//1024} KiB pal {len(bank.pal)} B; total {(len(header)+len(blob))//1024} KiB')

if __name__ == '__main__':
    ids = sys.argv[1:] or ['lvl00a', 'lvl01a', 'lvl02a', 'lvl03a', 'lvl04a']
    for i in ids: main(i, int(i[3:5]))
    json.dump(MUSIC, open(os.path.join(os.path.dirname(__file__), 'music.json'), 'w'))
    json.dump(SOUNDS, open(os.path.join(os.path.dirname(__file__), 'level_sounds.json'), 'w'))
