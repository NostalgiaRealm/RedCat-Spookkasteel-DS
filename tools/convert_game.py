"""Second half of the converter: actors, animation, objects, compiled scripts, motions and the light grid (lN.gam)."""
import json, math, os, re, struct, collections, heapq, subprocess
import numpy as np
from PIL import Image

KINDS = dict(DoorModel=1, ButtonModel=2, Trigger=3, CameraTrigger=3, ModelController=4, SavePoint=5, Fairy=6, AdamAnyActor=7, MovingEnemy=9, StandingEnemy=9, EffectSpoutEntity=10, EffectBeamEntity=11, EffectCoronaEntity=12)
E_ = 'effects/'
SPRITES = [E_+'fleuri-fleuri_a.png', E_+'fleurl8-fleurl8_a.png', E_+'fleurl7-fleurl7_a.png', E_+'star-star_a.png', E_+'flame03-a_flame.png', E_+'smoke_blue-smoke_green_a.png', E_+'smoke_brown-smoke_green_a.png', E_+'smoke_red-smoke_green_a.png', E_+'smoke_green-smoke_green_a.png',
           E_+'kaboom2-kaboom2_a.png', E_+'expl_gen03_yel-expl_gen_a_03.png', E_+'rcsdw-rcsdw_a.png', 'hud/target.png', E_+'score0-score0_a.png', E_+'score1-score1_a.png', E_+'score2-score2_a.png', E_+'score3-score3_a.png', E_+'score4-score4_a.png',
           E_+'beam-beam_a.png', E_+'energybeam-energybeam_a.png', E_+'coreff-coreff_a.png', E_+'ring-ring_a.png', 'projectiles/spark-00.png', 'projectiles/power-00.png', 'projectiles/super-00.png', 'projectiles/bone-00.png', 'projectiles/goo-00.png',
           'projectiles/jesterBall-00.png', 'projectiles/magicBall-00.png', 'projectiles/magma-00.png', 'projectiles/mushroom-00.png', 'projectiles/skull-00.png']
SP_SMOKE = {'smoke_blue.bmp': 5, 'smoke_brown.bmp': 6, 'smoke_red.bmp': 7, 'smoke_green.bmp': 8}
SP_SHOT = dict(RcEnemyShot=22, RcBone=25, RcGoo=26, RcPoison=26, RcJesterBall=27, RcMagicBall=28, RcMagma=29, RcMushRoom=30, RcSkull=31)
ENEMY_SHOT = dict(spider='RcEnemyShot', frog='RcPoison', plant='RcGoo', skeleton='RcBone', gargoyle='RcEnemyShot', brutusm='RcMushRoom', brutusb='RcBone', maxd='RcMagma', maxj='RcJesterBall', witch='RcMagicBall')
SOUND_PREFIX = dict(spiderg='spiderl', spidery='spiderll', spiderr='spiderlll', knight='knight', guardian='wachter', zombie='zombie', frog='kikker', skeleton='bones', gargoyle='gargoyl', plant='plant', ghostg='ghostl', ghosty='ghostll', ghostr='ghostlll', batg='batl', baty='batll', batr='batlll')
BAT_IDLE = dict(batg='bafxi1', baty='baiifx1', batr='baiiifx1')
BOSS_VOICE = dict(brutusm=('brin0011', 'brin0003', 'brin0004'), brutusb=('brin0011', 'brin0003', 'brin0004'), maxd=('mxin0011', 'mxin0003', 'mxin0004'), maxj=('mxin0002', 'mxin0003', 'mxin0004'), witch=('grizatt1', 'grizhit1', None))
DOOR_PANELS = dict(lvl01a=r'^(?:(?:secret)?door|fence$)', lvl02a=r'^(?:mazedeur1_dm$|kerk_[12]_(?:links|rechts)$|froggerdeur_(?:links|rechts)$)', lvl03a=r'^door_(?:left|right)_(?:walab01|airlab01)$')
ITEMS = dict(ItemCoin=0, ItemHealth=1, ItemPotion=2, ItemMirror=3, ItemLife=4, ItemHart=5, ItemHartContainer=5)
CLASSES = ['other', 'CameraTrigger', 'TeleporterFX', 'EffectSound', 'EffectMusic', 'EffectEndPoint', 'PlayerStart', 'World', 'SavePoint', 'FlashEffect', 'JesterMaxPoint']
MOVING = {1: ('spider', 'spider'), 2: ('bat', 'bat'), 3: ('ghost', 'ghost'), 4: ('zombie', 'zombie'), 5: ('frog', 'frog'), 6: ('skeleton', 'skeleton'), 7: ('brutusm', 'mushroombrutus'), 8: ('brutusb', 'bonebrutus'), 9: ('witch', 'witch'), 10: ('guardian', 'guardian')}
STANDING = {1: ('plant', 'plant'), 2: ('knight', 'knight'), 3: ('gargoyle', 'gargoyle'), 4: ('maxj', 'jestermax'), 5: ('maxd', 'dungeonmax')}
ENEMY_BASES = ['spider', 'bat', 'ghost', 'zombie', 'frog', 'skeleton', 'brutusm', 'brutusb', 'witch', 'guardian', 'plant', 'knight', 'gargoyle', 'maxj', 'maxd']
ENEMY_ACTOR = {'brutusm': 'brutusm', 'brutusb': 'brutusb'}
EVENTS = ['AfterDestroyCommand', 'AfterFlashCommand', 'AfterSwitchOffCommand', 'AfterSwitchOnCommand', 'CommandOnEnter', 'CommandOnLeave', 'DoorAfterCloseCommand', 'DoorAfterOpenCommand', 'DoorBeforeCloseCommand', 'DoorBeforeOpenCommand', 'DoorIsLockedCommand', 'MotionCommand', 'OnFlashCommand', 'OnHitCommand', 'OnInitCommand', 'OnTouchCommand', 'OnPlayerRespawnCommand', 'PickupCommand', 'RepeatCommand', 'RepeatCommand2', 'RepeatCommand3', 'RotateCommands', 'StartCommand', 'StopCommand', 'SwitchOffCommand', 'SwitchOnCommand']
VERBS = ['getone', 'open', 'close', 'lock', 'unlock', 'enable', 'disable', 'show', 'hide', 'switch', 'switchon', 'switchoff', 'trigger', 'destroy', 'setto', 'moveto', 'setmotionspeed', 'multiplymotionspeed', 'playambient', 'playaction', 'playspecial', 'playvictory', 'stopplaying', 'reset', 'rotatex', 'rotatey', 'rotatez', 'multiplyvolume']
NATIVES = ['startcutscene', 'stopcutscene', 'cutscenesay', 'communicatorsay', 'say', 'rchide', 'rcshow', 'playervisible', 'freezeenemies', 'unfreezeenemies', 'rcshowatspawnpoint', 'rcsetsavepoint', 'playwavsound', 'rchasallpotions', 'rcenableskill', 'getgametype', 'giveplayerkeyitem', 'playerhaskeyitem', 'killplayer', 'respawnplayer', 'rchintmotion']
FX = 4096
CLIP_RATE = 10

def rgb15(c):
    r, g, b = [max(0, min(31, int(round(float(x)*31)))) for x in c]
    return r | (g << 5) | (b << 10)
def n(v, f=0.0):
    try: return float(v)
    except (TypeError, ValueError): return f
def v3(s):
    try: p = [float(x) for x in str(s).split()]
    except ValueError: return [0, 0, 0]
    return (p+[0, 0, 0])[:3]

# ---------- decimation: quadric edge collapse keeping texture seams ----------
def decimate(pos, uv, joints, tris, target, strict=False):
    if len(tris) <= target: return tris, None
    pos = np.array(pos, float); nv = len(pos)
    Q = np.zeros((nv, 4, 4)); seam = collections.Counter()
    for t in tris:
        a, b, c = pos[list(t)]
        nrm = np.cross(b-a, c-a); l = np.linalg.norm(nrm)
        if l < 1e-9: continue
        nrm /= l; p = np.append(nrm, -nrm @ a); K = np.outer(p, p)*l
        for v in t: Q[v] += K
    edges = collections.Counter()
    for t in tris:
        for i in range(3): edges[tuple(sorted((t[i], t[(i+1) % 3])))] += 1
    border = {v for e, c in edges.items() if c == 1 for v in e}   # uv seams and holes stay
    parent = list(range(nv))
    def find(x):
        while parent[x] != x: parent[x] = parent[parent[x]]; x = parent[x]
        return x
    def cost(a, b):
        q = Q[a]+Q[b]; h = np.append(pos[b], 1)
        c = float(h @ q @ h)
        if a in border: c += 1e4
        if joints[a] != joints[b]: c += 2.0
        return c
    heap = [(cost(*e), e[0], e[1]) for e in edges]
    # collapse a into b in both directions considered by cost order
    heap += [(cost(e[1], e[0]), e[1], e[0]) for e in edges]
    if strict: heap = [h for h in heap if h[1] not in border]   # uv seams never move in the far level of detail
    heapq.heapify(heap)
    live = [list(t) for t in tris]; count = len(live)
    while heap and count > target:
        c, a, b = heapq.heappop(heap); a2, b2 = find(a), find(b)
        if a2 != a or a2 == b2: continue
        parent[a2] = b2; Q[b2] += Q[a2]
        new = []
        for t in live:
            f = [find(v) for v in t]
            if f[0] != f[1] and f[1] != f[2] and f[0] != f[2]: new.append(f)
        live = new; count = len(live)
    return [tuple(t) for t in live], None

# ---------- animation sampling (same formulas as the main port's animation.js) ----------
def quat_mat(q):
    x, y, z, w = q; xx, yy, zz, xy, xz, yz, wx, wy, wz = 2*x*x, 2*y*y, 2*z*z, 2*x*y, 2*x*z, 2*y*z, 2*w*x, 2*w*y, 2*w*z
    return np.array([[1-yy-zz, xy-wz, xz+wy], [xy+wz, 1-xx-zz, yz-wx], [xz-wy, yz+wx, 1-xx-yy]])
def sample_channel(ch, t, comps):
    times = ch['times']; vals = ch['values']; k = len(times)
    if not k: return None
    if t <= times[0] or k < 2: i = 0; j = 0; f = 0
    elif t >= times[-1]: i = j = k-1; f = 0
    else:
        hi = next(x for x in range(k) if times[x] > t); i = hi-1; j = hi; f = (t-times[i])/max(1e-9, times[j]-times[i])
    a = np.array(vals[i*comps:i*comps+comps]); b = np.array(vals[j*comps:j*comps+comps])
    if comps == 4 and a @ b < 0: b = -b
    r = a+(b-a)*f
    if comps == 4: r /= max(1e-9, np.linalg.norm(r))
    return r
def bone_pose(data, clip, t):
    tracks = {x['bone']: x for x in clip['tracks']} if clip else {}
    out = []
    for b in data['bones']:
        tr = tracks.get(b['name']); a = np.array(b['attachment'])
        A = a[:9].reshape(3, 3); at = a[9:12]
        p = sample_channel(tr['translation'], t, 3) if tr and tr.get('translation') else np.zeros(3)
        q = sample_channel(tr['rotation'], t, 4) if tr and tr.get('rotation') else np.array([0, 0, 0, 1.])
        if p is None: p = np.zeros(3)
        if q is None: q = np.array([0, 0, 0, 1.])
        R = quat_mat(q); M = A @ R; T = A @ p + at
        if b['parent'] >= 0:
            PR, PT = out[b['parent']]; M, T = PR @ M, PR @ T + PT
        out.append((M, T))
    return out
def euler_x(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])

GXC = dict(MTX_RESTORE=0x14, NORMAL=0x21, TEXCOORD=0x22, VTX16=0x23, POLYFMT=0x29, TEXIMAGE=0x2A, PLTT=0x2B, BEGIN=0x40, END=0x41)
def pack(cmds):
    out = []; i = 0
    while i < len(cmds):
        g = cmds[i:i+4]; i += 4; h = 0
        for k, (c, _) in enumerate(g): h |= c << (8*k)
        out.append(h)
        for _, ps in g: out.extend(x & 0xFFFFFFFF for x in ps)
    return out
POLY = lambda keyed: (31 << 16) | (3 << 6) | (1 << 15) | (1 << 11) | (1 << 12 if False else 0) | (2 << 24)

def load_actor_json(src, name):
    p = f'{src}/assets/actors/{name}.json'
    return json.load(open(p)) if os.path.exists(p) else None

def build_model(src, name, bank, target, max_bones=25, tex_size=64, clips=None, colors=16, rate=6):
    a = load_actor_json(src, name)
    if not a: return None
    s = a.get('settings', {}); scale = s.get('scale', 1) or 1
    rot = (s.get('initialRotationDegrees') or [-90, 0, 0])
    R0 = euler_x(rot[0])
    pos = np.array(a['positions'], float).reshape(-1, 3); uvs = np.array(a['uvs'], float).reshape(-1, 2)
    idx = a['indices']; joints = list(a.get('joints') or [-1]*len(pos))
    local = np.array(a.get('localPositions') or a['positions'], float).reshape(-1, 3)
    normals = np.array(a.get('normals') or [0, 1, 0]*len(pos), float).reshape(-1, 3)
    localn = np.array(a.get('localNormals') or a.get('normals') or [0, 1, 0]*len(pos), float).reshape(-1, 3)
    # material per triangle
    tri_mat = []
    groups = a.get('groups') or [dict(start=0, count=len(idx), materialIndex=0)]
    for g in groups:
        for k in range(g['start'], g['start']+g['count'], 3): tri_mat.append((k, g.get('materialIndex', 0)))
    anims = a.get('animations') or []
    animated = len(anims) > 0 and name not in STATIC_POSE
    infos = []
    for m in a['materials']:
        tex = m.get('texture')
        if tex:
            path = f'{src}/assets/actors/{tex}'
            keyed = (np.array(Image.open(path).convert('RGBA'))[:, :, 3] < 128).any()
            infos.append(bank.add_256('a:'+tex, path, bool(keyed), tex_size) if colors == 256 else bank.add('a:'+tex, path, bool(keyed), tex_size))
        else: infos.append(None)
    # decimate per material
    tris_by_mat = collections.defaultdict(list)
    for k, mi in tri_mat: tris_by_mat[mi].append((idx[k], idx[k+1], idx[k+2]))
    total = sum(len(v) for v in tris_by_mat.values()); out_tris = []; far_tris = []
    for mi, ts in tris_by_mat.items():
        tgt = max(8, int(target*len(ts)/max(1, total)))
        ts2, _ = decimate(pos, uvs, joints, ts, tgt)
        out_tris.extend((mi, t) for t in ts2)
        if len(ts2) >= 40:
            ts3, _ = decimate(pos, uvs, joints, ts2, max(16, int(len(ts2)*.4)), strict=True)
            far_tris.extend((mi, t) for t in ts3)
        else: far_tris.extend((mi, t) for t in ts2)
    # bones in use
    used = collections.Counter(joints[v] for _, t in out_tris+far_tris for v in t if joints[v] >= 0)
    frames = []; clip_table = []
    ref = next((c for c in anims if c['name'].lower() == 'idle'), anims[0] if anims else None)
    bones0 = bone_pose(a, ref, 0) if a.get('bones') else []
    final_scale = scale
    if animated and used:
        keep = sorted(used, key=lambda j: -used[j])[:max_bones]; keepset = set(keep)
        def remap(j):
            while j >= 0 and j not in keepset: j = a['bones'][j]['parent']
            return j if j >= 0 else keep[0]
        slot = {j: i for i, j in enumerate(sorted(keepset))}
        # vertices moved to a kept ancestor
        vlocal = {}
        for _, t in out_tris+far_tris:
            for v in t:
                j = joints[v]
                if j < 0: j2 = keep[0]; world = pos[v]; wn = normals[v]
                else:
                    j2 = remap(j); M, T = bones0[j]; world = M @ local[v] + T; wn = M @ localn[v]
                M2, T2 = bones0[j2]; inv = np.linalg.inv(M2)
                vlocal[v] = (slot[j2], inv @ (world-T2), inv @ wn)
        order = sorted(keepset)
        for c in anims:
            nm = c['name'].lower()
            if clips is not None and nm not in clips: continue
            nf = max(1, int(round(c['duration']*rate)))
            fr = bytearray()
            for f in range(nf):
                pose = bone_pose(a, c, f/rate)
                for j in order:
                    M, T = pose[j]
                    M2 = R0 @ M; T2 = R0 @ T * scale * 64
                    vals = [M2[0, 0], M2[1, 0], M2[2, 0], M2[0, 1], M2[1, 1], M2[2, 1], M2[0, 2], M2[1, 2], M2[2, 2]]
                    fr += struct.pack('<12h', *[max(-32768, min(32767, int(round(x*4096)))) for x in vals], *[max(-32768, min(32767, int(round(x*4096/4096)))) for x in T2*1])
            clip_table.append((nm, nf, bytes(fr)))
        # Authored actors without a MotionName stay in their bind pose.
        pose = bone_pose(a, None, 0); fr = bytearray()
        for j in order:
            M, T = pose[j]; M2 = R0 @ M; T2 = R0 @ T * scale * 64
            vals = [M2[0, 0], M2[1, 0], M2[2, 0], M2[0, 1], M2[1, 1], M2[2, 1], M2[0, 2], M2[1, 2], M2[2, 2]]
            fr += struct.pack('<12h', *[max(-32768, min(32767, int(round(x*4096)))) for x in vals], *[max(-32768, min(32767, int(round(x)))) for x in T2])
        clip_table.append(('bind', 1, bytes(fr)))
    else:
        order = []; vlocal = None
    # geometry lists
    bb = []
    def geometry(tris):
        cmds = []
        cur_mat = None; cur_slot = None
        tris = sorted(tris, key=lambda it: (it[0], min(vlocal[v][0] for v in it[1]) if vlocal else 0))
        began = False
        for mi, t in tris:
            if mi != cur_mat:
                if began: cmds.append((GXC['END'], [])); began = False
                info = infos[mi] if mi < len(infos) else None
                if info: cmds.append((GXC['TEXIMAGE'], [info['param']])); cmds.append((GXC['PLTT'], [info['pltt']]))
                else: cmds.append((GXC['TEXIMAGE'], [0]))
                cmds.append((GXC['POLYFMT'], [1 | (31 << 16) | (3 << 6) | (1 << 15) | (1 << 11) | (2 << 24)]))
                cur_mat = mi
                cmds.append((GXC['BEGIN'], [0])); began = True
            info = infos[mi] if mi < len(infos) else None
            for v in t:
                uv = uvs[v]
                if info:
                    u = int(round(uv[0]*info['w']*16)); w = int(round((1-uv[1])*info['h']*16))
                    cmds.append((GXC['TEXCOORD'], [(u & 0xFFFF) | ((w & 0xFFFF) << 16)]))
                if vlocal:
                    sl, lp, ln = vlocal[v]
                    if sl != cur_slot: cmds.append((GXC['MTX_RESTORE'], [3+sl])); cur_slot = sl
                    p = lp*scale*64
                else:
                    p = (R0 @ pos[v])*scale*64; ln = R0 @ normals[v]
                l = max(1e-6, float(np.linalg.norm(ln))); nx, ny, nz = [max(-511, min(511, int(round(c/l*511)))) for c in ln]
                cmds.append((GXC['NORMAL'], [(nx & 1023) | ((ny & 1023) << 10) | ((nz & 1023) << 20)]))
                bb.append(R0 @ (pos[v]) * scale)
                x, y, z = [max(-32768, min(32767, int(round(c)))) for c in p]
                cmds.append((GXC['VTX16'], [(x & 0xFFFF) | ((y & 0xFFFF) << 16), z & 0xFFFF]))
        if began: cmds.append((GXC['END'], []))
        return pack(cmds)
    words = geometry(out_tris)
    words_far = geometry(far_tris) if len(far_tris) < len(out_tris) else words
    bb = np.array(bb) if bb else np.zeros((1, 3))
    mn = bb.min(0); mx = bb.max(0)
    radius = float(max(np.abs(bb[:, 0]).max(), np.abs(bb[:, 2]).max()))
    if a.get('bounds'):
        lo = a['bounds']['min']; hi = a['bounds']['max']
        corners = np.array([[(hi if m & 1 else lo)[0], (hi if m & 2 else lo)[1], (hi if m & 4 else lo)[2]] for m in range(8)], float)
        corners = (R0 @ corners.T).T*scale; bmin = corners.min(0); bmax = corners.max(0)
    else: bmin, bmax = mn, mx
    return dict(name=name, rate=rate, words=words, words_far=words_far, polys_far=len(far_tris), polys=len(out_tris), bones=len(order), clips=clip_table, min=mn, max=mx, radius=radius, settings=s, height=float(mx[1]), scale=scale, bmin=bmin, bmax=bmax)

STATIC_POSE = {'torch'}
ENEMY_CLIPS = {'idle', 'idle0', 'idle1', 'idle2', 'idle3', 'idleflying', 'static', 'walkfw', 'walk', 'shoot1', 'shoot', 'hit', 'death', 'die', 'start', 'charge', 'teleport'}
PLAYER_CLIPS = {'idle', 'walkfw', 'walkbw', 'strafel', 'strafer', 'jump1', 'jump2', 'fall1', 'shoot1', 'death', 'hit', 're-spawn'}

def settings_dump(src, path):
    if not os.path.exists(path):
        code = "import('./source/gameplay-settings.js').then(m=>process.stdout.write(JSON.stringify(m.GAMEPLAY_SETTINGS)))"
        open(path, 'w').write(subprocess.check_output(['node', '-e', code], cwd=src).decode())
    return json.load(open(path))

def build_game(level_id, index, d, bank, light, src, out, music_table, sounds):
    settings = settings_dump(src, os.path.join(os.path.dirname(__file__), 'settings.json'))
    prog = json.load(open(f'{src}/data/davi/{level_id}.json')); mot = json.load(open(f'{src}/data/motions/{level_id}.json'))
    strings = {}; strlist = []
    def sid(s):
        s = str(s)
        if s not in strings: strings[s] = len(strlist); strlist.append(s)
        return strings[s]
    names = {str(e['%name%']).lower(): int(e['Model']) for e in d['entities'] if e.get('classname') == '%Model%' and 'Model' in e}
    motions_by_model = {}
    motion_list = []
    for m in mot['motions']:
        motions_by_model[m.get('model')] = m
    # ---------- which objects does the script name? ----------
    script_names = set()
    for rec in prog['objects']: script_names.add(rec['name'].lower())
    models = {}; model_order = []
    def model_id(file):
        stem = str(file or '').split('/')[-1].split('\\')[-1].lower().replace('.act', '')
        if not stem: return -1
        if stem in models: return models[stem]
        # RedCat keeps every polygon, the most bones the matrix stack holds and a 256x256, 256-colour texture.
        hero = stem == 'redcat'
        m = build_model(src, stem, bank, 100000 if hero else 190, max_bones=28 if hero else 25, tex_size=256 if hero else 64, clips=PLAYER_CLIPS if hero else ENEMY_CLIPS if stem in ENEMY_NAMES else None, colors=256 if hero else 16, rate=10 if hero else 6)
        if m is None or m['polys'] == 0 or len(m['words']) > 65000:
            print('  skipped model', stem, 'polys', m and m['polys'], 'words', m and len(m['words'])); models[stem] = -1; return -1
        models[stem] = len(model_order); model_order.append(m); return models[stem]
    ENEMY_NAMES = {'spider', 'spiderg', 'spidery', 'spiderr', 'bat', 'batg', 'baty', 'batr', 'ghostg', 'ghosty', 'ghostr', 'zombie', 'frog', 'skeleton', 'brutusm', 'brutusb', 'witch', 'guardian', 'plant', 'knight', 'gargoyle', 'maxj', 'maxd'}
    model_id('redcat')
    objects = []; name_objs = collections.defaultdict(list); scripted = collections.defaultdict(list)
    # Name lookup as in Gameplay.find: Davi-Script names outrank editor labels.
    by_script = collections.defaultdict(list); by_label = collections.defaultdict(list)
    for e in d['entities']:
        if not e.get('classname') or e['classname'].startswith('%'): continue
        for k in ('DaviName', 'DaviNameGroup'):
            if e.get(k): by_script[str(e[k]).lower()].append(e)
        if e.get('%name%'): by_label[str(e['%name%']).lower()].append(e)
    def find_entity(name):
        k = str(name or '').lower(); m = by_script.get(k) or by_label.get(k)
        return m[0] if m else None
    def find_pos(name):
        e = find_entity(name)
        return v3(e.get('Origin') or e.get('origin')) if e else None
    wp_ents = [e for e in d['entities'] if e.get('classname') == 'GrobberPathPoint']
    wp_list = [v3(e.get('Origin')) for e in wp_ents]; wp_index = {}
    for i, e in enumerate(wp_ents):
        for k in (e.get('%name%'), e.get('DaviName')):
            if k: wp_index[str(k).lower()] = i
    wp_explicit = [[wp_index[str(e.get(f'WayPoint{k}')).lower()] for k in (1, 2, 3, 4) if str(e.get(f'WayPoint{k}') or '').lower() in wp_index] for e in wp_ents]
    stat_keys = []
    cams = []; cam_points = bytearray(); ncam_points = 0; winds = bytearray(); nwinds = 0; waypoints = bytearray(); nwp = 0; pending_cams = []
    sound_list = sounds.setdefault(str(index), [])
    item_actor = dict(ItemCoin='imoney', ItemHealth='ihealth')
    for i, e in enumerate(d['entities']):
        cls = e.get('classname')
        if not cls or cls.startswith('%'): continue
        kind = KINDS.get(cls, 8 if cls in ITEMS else 0)
        nm = [str(e.get(k) or '').lower() for k in ('DaviName', '%name%', 'DaviNameGroup')]
        named = any(x in script_names for x in nm if x)
        keep = kind != 0 or named or cls in ('EffectEndPoint', 'EffectMusic', 'World', 'PlayerStart', 'TeleporterFX', 'FlashEffect', 'JesterMaxPoint')
        if not keep: continue
        p = [0]*8; model = -1; yaw = 0
        flags = 0
        enabled = n(e['IsInitiallyEnabled'], 1) != 0 if 'IsInitiallyEnabled' in e else (n(e['Active'], 1) != 0 if 'Active' in e else True)
        if enabled: flags |= 1
        if n(e.get('IsInitiallyOpen')) != 0: flags |= 2
        if n(e.get('IsInitiallyLocked')) != 0: flags |= 4
        if n(e.get('IsInitiallySwitchedOn')) != 0: flags |= 8
        if n(e.get('IsSecret')) != 0: flags |= 16
        if str(e.get('PlayerCollision')) == '0': flags |= 32
        brush = names.get(str(e.get('Model') or '').lower(), -1)
        if kind == 1:
            p[0] = int(n(e.get('TimeToStayOpen'), -1)*16); p[1] = int(n(e.get('TriggerRadius'))); p[2] = int(n(e.get('TouchToOpen')))
            # door-audio.js: invisible barriers and carriers are silent, panels of type 4 only by name
            has_surface = any(g['model'] == brush and g['count'] > 0 and g['alpha'] > 0 and not g['flags'] & 4 for g in d['groups'])
            dt_ = int(n(e.get('DoorType'))); dn_ = str(e.get('DaviName') or '')
            p[3] = 0
            if has_surface:
                if dt_ in (1, 2, 3): p[3] = dt_
                elif dt_ == 4 and level_id in DOOR_PANELS and re.search(DOOR_PANELS[level_id], dn_, re.I): p[3] = 3 if re.search('secret', dn_, re.I) or n(e.get('IsSecret')) else 1
            p[4] = int(n(e.get('UserFeedBack'))); p[5] = dt_
        if kind == 2:
            p[0] = int(n(e.get('MaxSwitchTimes'), -1)); p[1] = int(n(e.get('TriggerRadius'))); p[2] = int(n(e.get('TouchToSwitch'))); p[3] = int(n(e.get('ShootToSwitch'))); p[4] = int(n(e.get('ButtonType')))
        if kind == 3:
            p[0] = int(n(e.get('MaxTriggerTimes'), -1)); p[1] = int(n(e.get('MinTriggerTimes'), -1)); p[2] = int(n(e.get('DamagePerSecond'))*16)
            ts = str(e.get('TargetSubLevel') or '').lower().replace('.bsp', ''); p[3] = int(ts[3:5]) if ts.startswith('lvl') and ts[3:5].isdigit() else (100 if ts else -1)
            p[4] = int(n(e.get('TriggerRadius'), 24))
            if cls == 'CameraTrigger':
                pts = []
                for k in range(int(n(e.get('CamScriptPosTotal')))):
                    q = find_pos(e.get(f'CamScriptPos{k}'))
                    if q: pts.append(q)
                first = ncam_points
                for q in pts: cam_points += struct.pack('<3i', *[int(v*FX) for v in q]); ncam_points += 1
                mode = int(n(e.get('CameraMode'))); pos_ = find_pos(e.get('CamPos')) or v3(e.get('Origin')); tp = find_pos(e.get('CamTargetPos'))
                pending_cams.append((mode, len(pts), str(e.get('CamTargetDaviName') or ''), first, 0 if mode == 1 else max(0.0, n(e.get('CamTimeInMode'))/1000), v3(e.get('CamOffset') or '0 0 0'), pos_, tp))
                p[5] = len(pending_cams); p[7] = 1 if n(e.get('CamActivateOnEnable')) else 0
            w_ = v3(e.get('AddPlayerSpeed') or '0 0 0')
            if any(w_):
                winds += struct.pack('<3i', *[int(v*32*FX) for v in w_]); nwinds += 1; p[6] = nwinds
        if kind == 4:
            p[0] = int(n(e.get('RepeatMode'))); p[1] = int(n(e.get('InitialPosition'))*256)
        if kind == 8:
            p[0] = ITEMS[cls]; t = int(n(e.get('Type'), 1)); p[1] = t
            suffix = 'sml'[max(0, min(2, t-1))]
            model = model_id({0: 'imoney'+suffix, 1: 'ihealth'+suffix, 2: 'ipotion', 3: 'imirror', 4: 'ilife', 5: 'hartcontainer'}[p[0]])
            p[3] = int(n(e.get('RotateX'))*32768/360); p[4] = int(n(e.get('RotateZ'))*32768/360)
            yaw = int(n(e.get('RotateY'))*32768/360)
        if kind == 9:
            base, config = (STANDING if cls == 'StandingEnemy' else MOVING).get(int(n(e.get('Type'))), ('spider', 'spider'))
            variant = max(1, min(3, int(n(e.get('SubType'), 1))))
            colored = base in ('spider', 'bat', 'ghost')
            actor = base+'gyr'[variant-1] if colored else base
            if base == 'bat' and not load_actor_json(src, actor): actor = 'bat'
            model = model_id(actor)
            flying = base in ('bat', 'ghost', 'witch'); standing = cls == 'StandingEnemy'
            key_ = (base, config+(str(variant) if colored else ''), actor)
            if key_ not in stat_keys: stat_keys.append(key_)
            start_ = str(e.get('StartPoint') or '').lower()
            p = [ENEMY_BASES.index(base), stat_keys.index(key_), variant, wp_index.get(start_, -1), -1, 0, 0, (2 if flying else 0) | (4 if standing else 0)]
            # a zombie's grave: an explicit upward first edge from its start point
            if base == 'zombie' and str(e.get('IsInitiallyEnabled')) == '0' and n(e.get('UnlinkStartPoint')) and p[3] >= 0 and len(wp_explicit[p[3]]) == 1:
                t_ = wp_explicit[p[3]][0]
                if wp_list[t_][1] > wp_list[p[3]][1]: p[4] = t_
            # Clock-face orientation, as clockFaceActorYaw in the main port.
            yaw = int((0.5-n(e.get('StartOrientation'))/12)*32768)
            yaw = ((yaw+32768) % 65536)-32768
        if kind == 10:
            bm_ = str(e.get('BitmapFileName') or '').lower()
            p = [SP_SMOKE.get(bm_, 4), int(20*n(e.get('Scale'), 1)), int(n(e.get('SizePercentageStart'), 100)), int(n(e.get('SizePercentageEnd'), 100)), int(n(e.get('AlphaPercentageStart'), 100)), int(n(e.get('AlphaPercentageEnd'))), int((n(e.get('LifeSecondsMin'), 1)+n(e.get('LifeSecondsMax'), 1))*8), int((n(e.get('SpeedMin'))+n(e.get('SpeedMax')))*8)]
        if kind == 11:
            end_ = find_pos(e.get('BeamEndPoint')) or v3(e.get('Origin'))
            f_ = str(e.get('SoundTriggerFilename') or '').lower()
            if f_ and f_ not in sound_list: sound_list.append(f_)
            p = [int(end_[0]), int(end_[1]), int(end_[2]), int(n(e.get('DamagePerSecond'))*16), int(n(e.get('Width'), 5)), int(n(e.get('Type'))) | (4 if str(e.get('TriggerOnce')) == '1' else 0), int(n(e.get('TriggerDelay'))*16), sound_list.index(f_) if f_ else -1]
        if kind == 12:
            c_ = v3(e.get('Color') or '255 255 255')
            p = [rgb15([x/255 for x in c_]), int(n(e.get('RadiusMin'), 1)), int(n(e.get('RadiusMax'), 10)), int(n(e.get('RadiusDistanceMin'), 100)), int(n(e.get('RadiusDistanceMax'), 1000)), 0, 0, 0]
        if kind == 7:
            model = model_id(e.get('ActorFileName'))
            ms = model_order[model]['settings'] if model >= 0 else {}
            p[0] = (1 if ms.get('blocksPlayer') else 0) | (2 if ms.get('canBeShot') or n(e.get('Targetable')) else 0) | (4 if ms.get('destroyable') else 0) | (8 if n(e.get('Targetable')) and ms.get('canBeShot') else 0)
            p[1] = int(model_order[model]['radius']) if model >= 0 else 0
            th_ = ms.get('damageThreshold') or [1, 1]; p[2] = int(max(1, (th_[0]+th_[-1])/2)) if ms.get('destroyable') else 1
            ex_ = ms.get('explosion') or {}; p[5] = int(min(127, n(ex_.get('SizePercentage'), 25))) | (int(min(127, n(ex_.get('DamagePercentage'), 25))) << 8) if n(ex_.get('NrExplosions'), 1) > 0 else 0
            p[3] = int(n(e.get('RotateX'))*32768/360); p[4] = int(n(e.get('RotateZ'))*32768/360)
            yaw = int(n(e.get('RotateY'))*32768/360)
            motion_name = str(e.get('MotionName') or '').lower(); clip_i = -1
            if model >= 0:
                names_ = [c[0] for c in model_order[model]['clips']]
                if motion_name in names_: clip_i = names_.index(motion_name)
                elif model_order[model]['name'] == 'htorch' and names_: clip_i = 0
                elif 'bind' in names_: clip_i = names_.index('bind')
            p[6] = clip_i; p[7] = int(n(e.get('MotionSpeed'), 1)*256)
        if cls == 'Fairy':
            pts = []
            for k in range(min(10, int(n(e.get('NumberOfWayPoints'))))):
                q = find_pos(e.get(f'FairyWP{k}'))
                if q: pts.append(q)
            p[0] = int(n(e.get('LifeTime'))*16); p[1] = len(pts); p[2] = nwp
            for q in pts: waypoints += struct.pack('<3f', *q); nwp += 1
        if cls == 'EffectSound':
            f = str(e.get('SoundFileName') or '').lower(); p[1] = 1 if n(e.get('Replay')) else 0; p[0] = -1
            if f:
                if f not in sound_list: sound_list.append(f)
                p[0] = sound_list.index(f)
            p[2] = int(n(e.get('MinReplayDelaySeconds'))*16); p[3] = int(n(e.get('MaxReplayDelaySeconds'))*16); p[4] = 1 if n(e.get('Use3DSound')) else 0
        if cls == 'EffectMusic':
            for k, key in enumerate(('MusicAmbient', 'MusicAction', 'MusicSpecial')):
                f = str(e.get(key) or '').lower()
                if f and f not in music_table: music_table.append(f)
                p[k] = music_table.index(f) if f else -1
        c = CLASSES.index(cls) if cls in CLASSES else 0
        motion = -1
        if kind in (1, 2, 4) and brush in motions_by_model:
            m = motions_by_model[brush]
            if m not in motion_list: motion_list.append(m)
            motion = motion_list.index(m)
        pos = v3(e.get('Origin') or e.get('origin'))
        ids = [sid(x) if x else 0xFFFF for x in nm]
        oid = len(objects)
        scale_fp = 256
        if model >= 0 and n(e.get('Scale')) > 0: scale_fp = int(round(n(e.get('Scale'))/(model_order[model]['scale'] or 1)*256))
        objects.append(struct.pack('<BBH3i4h3H8hH', kind, c, flags, *[int(v*FX) for v in pos], yaw, brush, model, motion, *ids, *[int(max(-32768, min(32767, x))) for x in p], scale_fp))
        for raw in (e.get('DaviName'), e.get('%name%'), e.get('DaviNameGroup')):
            if raw: name_objs[str(raw).lower()].append(oid)
        for raw in (e.get('DaviName'), e.get('DaviNameGroup')):
            if raw: scripted[str(raw).lower()].append(oid)
    camsbin = bytearray()
    for mode, np_, target, first, dur, off, pos_, tp in pending_cams:
        k = target.lower(); tl = scripted.get(k) or name_objs.get(k) or []
        camsbin += struct.pack('<hhhHi3i3i3ii', mode, np_, tl[0] if (target and tl) else -1, first, int(dur*FX), *[int(v*FX) for v in off], *[int(v*FX) for v in pos_], *[int(v*FX) for v in (tp or [0, 0, 0])], 1 if tp else 0)
    # ---------- enemy statistics per difficulty, waypoints, level totals ----------
    def lsound(name):
        if not name: return -1
        f = name+'.wav'
        if f not in sound_list: sound_list.append(f)
        return sound_list.index(f)
    statbin = bytearray()
    for base, cfg, actor in stat_keys:
        sounds_ = [-1]*6
        if base in BOSS_VOICE:
            v_ = BOSS_VOICE[base]; sounds_[3] = lsound(v_[0]); sounds_[4] = lsound(v_[1]); sounds_[5] = lsound(v_[2])
        elif actor in SOUND_PREFIX:
            q_ = SOUND_PREFIX[actor]
            sounds_ = [lsound(BAT_IDLE.get(actor) or q_+'1'), lsound(None if actor in BAT_IDLE else q_+'2'), lsound(q_+('4' if actor == 'plant' else '3')), lsound(q_+('3' if actor == 'plant' else '4')), lsound(q_+'5'), lsound(q_+'6')]
        for diff in ('Easy', 'Normal', 'Hard'):
            root = cfg if cfg in settings else base
            sec = settings.get(root, {}).get(diff) or settings.get(root, {}).get('Normal') or settings.get(base, {}).get(diff) or {}
            key = ENEMY_SHOT.get(base, 'RcEnemyShot'); ps_ = settings.get('projectile'+diff.lower(), {}).get(key, {})
            ranged = n(sec.get('AverageShotsPerSalvo')) > 0 or base == 'maxj'
            maxl = n(ps_.get('MaximumLifeTimeInSeconds'), n(ps_.get('MaximumLifeTime'), 5)); minl = min(maxl, n(ps_.get('MinimumLifeTimeInSeconds'), n(ps_.get('MinimumLifeTime'), maxl)))
            row = [max(1, n(sec.get('Health'), 3)), n(sec.get('Speed'), 55), n(sec.get('AttackRange'), 100), n(sec.get('MinPlayerDistance')), n(sec.get('VisualRange'), 250), n(sec.get('SenseRange'), 50), max(1, n(sec.get('Damage'), 1)), n(sec.get('PlayerScore'), 15),
                   n(sec.get('ViewAngle'), math.pi)*32768/(2*math.pi), min(32767, n(sec.get('RotationPerSec'), 3*math.pi)*32768/(2*math.pi)/4), n(sec.get('TimeToRememberVisual'), 2)*16, n(sec.get('DrawMotionPart'), .5)*256, n(sec.get('ShootMotionCollisionStart'), .5)*256, n(sec.get('ShootMotionCollisionEnd'), .7)*256,
                   n(sec.get('AverageShotsPerSalvo'), 2)*16, n(sec.get('WaitTimeBetweenShots'), .4)*16, n(sec.get('WaitTimeAfterSalvo'), .4)*16, n(sec.get('ChanceToMoveAfterSalvo'))*256, n(sec.get('FallSpeed'), 160), n(sec.get('RiseTime'), 1)*16, n(sec.get('LookTime'), 2)*16, n(sec.get('MinHeight')), n(sec.get('MaxHeight'), 40), n(sec.get('MachineScale'), 1.4)*16,
                   1 if ranged else 0, SP_SHOT.get(key, 22), n(ps_.get('InitialSpeed'), n(ps_.get('Speed'), 300)), n(ps_.get('Damage'), 1)*16, minl*16, maxl*16, n(ps_.get('Gravity'))*32, *sounds_, 0]
            statbin += struct.pack('<38h', *[int(max(-32768, min(32767, x))) for x in row])
    # explicit links, or else the nearest points the main port's graph would connect
    wpbin = bytearray()
    for i, q in enumerate(wp_list):
        links = list(wp_explicit[i])
        if not links:
            near = sorted((math.dist(q, r), j) for j, r in enumerate(wp_list) if j != i and math.dist(q, r) <= 300 and abs(q[1]-r[1]) <= 50 and n(wp_ents[i].get('SubSystemId')) == n(wp_ents[j].get('SubSystemId')))
            links = [j for _, j in near[:4]]
        links = (links+[-1]*4)[:4]
        wpbin += struct.pack('<3h4hh', int(q[0]), int(q[1]), int(q[2]), *links, 0)
    sub_ = settings.get('game', {}).get(f'sublevel1_{index+1}', {})
    shotrows = []
    for diff in ('easy', 'normal', 'hard'):
        for key, dmg, spd, inc in (('RcShot', 1, 300, 160), ('RcPowerShot', 2, 300, 160), ('RcSuperShot', 4, 240, 60)):
            r_ = settings.get('projectile'+diff, {}).get(key, {})
            shotrows += [int(n(r_.get('Damage'), dmg)*16), int(n(r_.get('InitialSpeed'), spd)), int(n(r_.get('SpeedIncreasePerSecond'), inc)), int(n(r_.get('MaximumSpeed'), 1000)), int(n(r_.get('MaximumLifeTimeInSeconds'), 5)*16)]
    metabin = struct.pack('<6i', int(n(sub_.get('NrPotions'), -1)), int(n(sub_.get('NrMoneyBags'), -1)), int(n(sub_.get('NrEnemies'), -1)), int(n(sub_.get('NrSecrets'), -1)), len(wp_list), len(stat_keys))+struct.pack(f'<{len(shotrows)}i', *shotrows)
    # ---------- script ----------
    funcs = []; fid = {}
    def key(r): return r.get('id') or f"{r['tag']:x}:{r['index']}"
    all_fn = list(prog['functions'])
    classes = prog.get('classes') or []
    for cl in classes:
        for m in cl.get('members', []): all_fn.append(m)
        for m in cl.get('members', []):
            for h in m.get('handlers', []) or []: all_fn.append(h)
    for f in all_fn: fid[key(f)] = len(fid)
    globs = list(prog.get('globals') or [])+list(prog.get('constants') or [])
    gid = {key(g): i for i, g in enumerate(globs)}
    objs = {key(o): o for o in prog['objects']}
    def tcode(t): return t if isinstance(t, int) else (t or {}).get('code', 0)
    binds = {}
    bind_list = []
    def bid(nm):
        k = nm.lower()
        if k not in binds:
            targets = scripted.get(k) or name_objs.get(k) or []
            seen = []; [seen.append(x) for x in targets if x not in seen]
            binds[k] = (len(binds), len(bind_list), len(seen), sid(k)); bind_list.extend(seen)
        return binds[k][0]
    def conv_expr(x, locals_):
        if not x: return (0, 0, 0)
        tag = x['tag']
        if tag == 0:
            ty = tcode(x.get('type')); v = x.get('value')
            if ty == 1: return (0, 1, struct.unpack('<i', struct.pack('<f', float(v or 0)))[0])
            if ty == 2: return (0, 2, int(v or 0))
            if ty == 3: return (0, 3, sid(v or ''))
            return (0, 4, -1)
        if tag == 1: return (1, tcode(x.get('type')), 0)
        if tag == 3: return (3, 0, 0)
        if tag == 5: return (5, 0, x['index'])
        r = x['ref']; rid = r.get('id') or '/'.join(f"{p['tag']:x}:{p['index']}" for p in reversed(r['path']))
        if tag == 2:
            if rid in objs: return (8, 4, bid(objs[rid]['name']))
            if rid in locals_: return (6, locals_[rid][1], locals_[rid][0])
            if rid in gid: return (7, tcode(globs[gid[rid]].get('type')), gid[rid])
            raise SystemExit('unresolved variable '+rid)
        if tag == 4:
            return (4, 0, fid[rid])
        raise SystemExit(f'operand {tag}')
    insbin = bytearray(); ninstr = 0
    fnbin = bytearray()
    for f in all_fn:
        k = f.get('kind'); params = f.get('parameters') or []; locs = f.get('locals') or []
        lmap = {}
        for i, q in enumerate(params+locs): lmap[key(q)] = (i, tcode(q.get('type')))
        self_i = next((i for i, q in enumerate(params) if q.get('name') == '@self'), -1)
        ptypes = [tcode(q.get('type')) for q in params]+[0]*8
        first = ninstr; native = -1
        kc = 0
        nmz = (f.get('name') or '').lower()
        if k in ('external', 'builtin'): kc = 1; native = NATIVES.index(nmz) if nmz in NATIVES else -1
        elif k == 'method': kc = 2; native = VERBS.index(nmz) if nmz in VERBS else -1
        elif k == 'event': kc = 3; native = EVENTS.index(f['name']) if f['name'] in EVENTS else -1
        for ins in f.get('instructions') or []:
            op = ins['opcode']; e0 = e1 = e2 = (0, 0, 0)
            if op == 1: e0 = conv_expr(ins.get('a'), lmap)
            if op == 2: e0 = conv_expr(ins.get('a'), lmap); e1 = conv_expr(ins.get('b'), lmap)
            if op == 3: e0 = conv_expr(ins.get('source'), lmap); e1 = conv_expr(ins.get('destination'), lmap)
            if op == 4: e0 = conv_expr(ins.get('source'), lmap); e1 = conv_expr(ins.get('rhs'), lmap); e2 = conv_expr(ins.get('destination'), lmap)
            if op == 5: e0 = conv_expr(ins.get('source'), lmap); e1 = conv_expr(ins.get('destination'), lmap)
            if op == 6: e0 = conv_expr(ins.get('source'), lmap); e1 = conv_expr(ins.get('destination'), lmap)
            target = ins.get('target', 0)
            insbin += struct.pack('<BBBBi', op, ins.get('operator', 0) or 0, 1 if ins.get('whenTruthy') else 0, 0, target)
            for ex in (e0, e1, e2): insbin += struct.pack('<BBhi', ex[0], ex[1], 0, ex[2])
            ninstr += 1
        fnbin += struct.pack('<BBBbbBhHH8B', kc, tcode(f.get('type')), len(params), self_i, 0, len(params)+len(locs), native, first, ninstr-first, *ptypes[:8])
    # handlers (owner name, event, function)
    handlers = []
    for cl in classes:
        for m in cl.get('members', []):
            for h in m.get('handlers', []) or []:
                if m['name'] not in EVENTS: continue
                owner = h['owner']; oid_ = owner.get('id') or '/'.join(f"{p['tag']:x}:{p['index']}" for p in reversed(owner['path']))
                ow = objs.get(oid_)
                if ow: handlers.append((sid(ow['name'].lower()), EVENTS.index(m['name']), fid[key(h)]))
    handlers.sort()
    init_fn = fid[key(next(f for f in prog['functions'] if f['name'] == '@init'))]
    globbin = b''.join(struct.pack('<Bxxxi', tcode(g.get('type')), (struct.unpack('<i', struct.pack('<f', float(g.get('value') or 0)))[0] if tcode(g.get('type')) == 1 else (sid(g.get('value') or '') if tcode(g.get('type')) == 3 else int(g.get('value') or 0) if tcode(g.get('type')) == 2 else -1))) for g in globs)
    # ---------- motions ----------
    mbin = bytearray(); mhead = bytearray()
    for m in motion_list:
        path = (m.get('paths') or [{}])[0]; tr = path.get('translation') or {'times': [], 'values': []}; ro = path.get('rotation') or {'times': [], 'values': []}
        evs = sorted(m.get('events') or [], key=lambda e: e['time'])
        off = len(mbin)
        for i, t in enumerate(tr['times']):
            mbin += struct.pack('<4i', int(t*FX), *[int(v*FX) for v in tr['values'][i*3:i*3+3]])
        roff = len(mbin)
        for i, t in enumerate(ro['times']):
            mbin += struct.pack('<i4h', int(t*FX), *[int(round(v*16384)) for v in ro['values'][i*4:i*4+4]])
        eoff = len(mbin)
        for e in evs:
            mbin += struct.pack('<iHH', int(e['time']*FX), sid(str(e.get('label') or '')), 0)
        origin = m.get('origin') or [0, 0, 0]
        mhead += struct.pack('<3i3i3I3H', int(m.get('startTime', 0)*FX), int(m.get('endTime', 0)*FX), int((m.get('playbackEndTime') or m.get('endTime', 0))*FX), *[int(v*FX) for v in origin], off, roff, eoff, len(tr['times']), len(ro['times']), len(evs))
        mhead += b'\0'*2
    # ---------- model blobs ----------
    lists = bytearray(); frames = bytearray(); mtab = bytearray(); cliptab = bytearray()
    for m in model_order:
        lo = len(lists); lists += struct.pack(f"<{len(m['words'])}I", *m['words'])
        lo2 = lo
        if m['words_far'] is not m['words']: lo2 = len(lists); lists += struct.pack(f"<{len(m['words_far'])}I", *m['words_far'])
        spin = [int(v*32768/360/4) for v in (m['settings'].get('rotationDegreesPerSecond') or [0, 0, 0])]  # a quarter binary angle per second
        co = len(cliptab)//16
        for nm, nf, data in m['clips']:
            fo = len(frames); frames += data
            cliptab += struct.pack('<8sHHI', nm.encode()[:8], nf, m['rate'], fo)
        mn, mx = m['min'], m['max']
        bb_ = [int(math.floor(x)) for x in m['bmin']]+[int(math.ceil(x)) for x in m['bmax']]
        mtab += struct.pack('<IHHBBH4h6hIHH4h', lo, len(m['words']), m['polys'], m['bones'], len(m['clips']), co, int(m['radius']), int(m['height']), int(mn[1]), 0, *bb_, lo2, len(m['words_far']), m['polys_far'], *spin, 0)
    # ---------- light grid ----------
    GS = 256
    lo = np.array(d['bounds']['min'], float); hi = np.array(d['bounds']['max'], float)
    gn = (np.ceil((hi-lo)/GS).astype(int)+1)
    acc = np.zeros((*gn, 3)); cnt = np.zeros(gn)
    for x, y, z, r, g, b in light:
        i, j, k = int((x-lo[0])//GS), int((y-lo[1])//GS), int((z-lo[2])//GS)
        if 0 <= i < gn[0] and 0 <= j < gn[1] and 0 <= k < gn[2]:
            acc[i, j, k] += (r, g, b); cnt[i, j, k] += 1
    grid = np.full((*gn, 3), -1.0)
    m = cnt > 0; grid[m] = acc[m]/cnt[m, None]
    default = acc.sum((0, 1, 2))/max(1, cnt.sum())
    for _ in range(int(gn.max())):
        empty = grid[:, :, :, 0] < 0
        if not empty.any(): break
        new = grid.copy()
        for ax in range(3):
            for sh in (1, -1):
                r = np.roll(grid, sh, axis=ax); fill = empty & (r[:, :, :, 0] >= 0) & (new[:, :, :, 0] < 0)
                new[fill] = r[fill]
        grid = new
    grid[grid[:, :, :, 0] < 0] = default
    gridbin = b''.join(struct.pack('<H', rgb15(np.clip(c*1.1+.08, 0, 1))) for c in grid.reshape(-1, 3))
    # ---------- assemble ----------
    stroff = bytearray(); strdata = bytearray()
    bindbin = bytearray(); 
    # bindings must be final before writing
    bindbin = b''.join(struct.pack('<HHH', first, count, nameid) for _, first, count, nameid in sorted(binds.values()))
    for s in strlist:
        stroff += struct.pack('<I', len(strdata)); strdata += s.encode('latin-1', 'replace')+b'\0'
    # name lookup for runtime strings (Trigger targets by name)
    allnames = sorted((sid_, nm) for nm, sid_ in [(k, sid(k)) for k in []])
    sections = [b''.join(objects), bytes(stroff), bytes(strdata), bindbin, struct.pack(f'<{len(bind_list)}H', *bind_list),
                b''.join(struct.pack('<HHH', *h) for h in handlers), bytes(fnbin), bytes(insbin), globbin, bytes(mhead), bytes(mbin),
                bytes(mtab), bytes(cliptab), bytes(lists), bytes(frames), gridbin,
                bytes(camsbin), bytes(cam_points), bytes(winds), bytes(waypoints), bytes(statbin), bytes(wpbin), metabin]
    counts = [len(objects), len(strlist), len(binds), len(handlers), len(all_fn), ninstr, len(globs), len(motion_list), len(model_order), init_fn, *[int(x) for x in gn], *[int(x) for x in lo], 0, 0]
    head = struct.pack('<16i', *(counts+[0]*16)[:16])
    pos = len(head)+4*len(sections); offs = []; blob = bytearray()
    for s in sections:
        pad = (-pos) & 3; blob += b'\0'*pad; pos += pad
        offs.append(pos); blob += s; pos += len(s)
    open(f'{out}/l{index}.gam', 'wb').write(head+struct.pack(f'<{len(offs)}I', *offs)+blob)
    print(f'  game: objects {len(objects)} strings {len(strlist)} bindings {len(binds)} handlers {len(handlers)} functions {len(all_fn)} instr {ninstr} motions {len(motion_list)} models {len(model_order)} '
          f'polys {[m["name"]+":"+str(m["polys"]) for m in model_order[:6]]} lists {len(lists)//1024}K frames {len(frames)//1024}K grid {len(gridbin)//1024}K total {(len(head)+len(blob))//1024}K vram {len(bank.vram)//1024}K')
