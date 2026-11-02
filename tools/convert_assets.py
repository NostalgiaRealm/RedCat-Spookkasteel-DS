#!/usr/bin/env python3
"""Sound (hardware IMA-ADPCM) and the bottom screen picture for the DS version."""
import json, os, struct, sys, wave
import numpy as np
from PIL import Image
SRC = os.path.join(os.path.dirname(__file__), '..', '..', 'RCSPOOK_NEW'); OUT = os.path.join(os.path.dirname(__file__), '..', 'nitrofiles')
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8]
def adpcm(pcm):
    pred = int(pcm[0]); index = 0; out = bytearray(struct.pack('<hH', pred, index)); nibble = None
    for s in pcm:
        step = STEP[index]; diff = int(s)-pred; code = 0
        if diff < 0: code = 8; diff = -diff
        delta = step >> 3
        if diff >= step: code |= 4; diff -= step; delta += step
        step2 = step >> 1
        if diff >= step2: code |= 2; diff -= step2; delta += step2
        step2 >>= 1
        if diff >= step2: code |= 1; delta += step2
        pred = max(-32767, min(32767, pred-delta if code & 8 else pred+delta))
        index = max(0, min(88, index+IDX[code & 7]))
        if nibble is None: nibble = code
        else: out.append(nibble | (code << 4)); nibble = None
    while len(out) % 4: out.append(0)
    return bytes(out)
def load(path, rate):
    w = wave.open(path); ch = w.getnchannels(); sw = w.getsampwidth(); sr = w.getframerate(); raw = w.readframes(w.getnframes())
    if sw == 1: a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32)-128)*256
    else: a = np.frombuffer(raw, dtype='<i2').astype(np.float32)
    a = a.reshape(-1, ch).mean(1)
    n = int(len(a)*rate/sr); x = np.linspace(0, len(a)-1, n)
    return np.interp(x, np.arange(len(a)), a).astype(np.int16)
def find(name):
    for root, _, files in os.walk(f'{SRC}/assets'):
        for f in files:
            if f.lower() == name.lower() and ('audio' in root or 'voices' in root): return os.path.join(root, f)
    raise SystemExit('missing sound '+name)
os.makedirs(OUT, exist_ok=True)
# same order as the S_ enum in source/game.h; sound choices follow gameplay-audio.js in the main port
SFX = ['rcjump1', 'rcshoot1', 'rcshoot2', 'rcshoot3', 'rcshoot4', 'rcshoot5', 'rcshoot6', 'rcgen1', 'rcgen2', 'rcgen3', 'rcgen7', 'rcland1', 'rcwalk1', 'rcwalk2', 'icoins', 'icoinm', 'icoinl', 'ihealths', 'ihealthm', 'ihealthl', 'ipotion', 'imirror', 'ilife', 'ihart', 'secretfound',
       'switchpushbutton', 'switchhandle', 'switchtrigger', 'opendoornormal', 'opendoorkey', 'opendoorsecret', 'errorsound', 'expl6', 'lockdoornormal', 'unlockdoornormal', 'lockdoorkey', 'unlockdoorkey', 'lockdoorsecret', 'unlockdoorsecret', 'gri5fx11', 'idlefee1', 'magiev12']
SFX = [x+'.wav' for x in SFX]
table = b''; blob = b''; RATE = 8000
for s in SFX:
    try: d = adpcm(load(find(s), RATE)[:RATE*3//2])
    except SystemExit: print('missing', s); d = b''
    table += struct.pack('<II', len(blob), len(d)); blob += d
open(f'{OUT}/sfx.bin', 'wb').write(struct.pack('<I', len(SFX))+table+blob)
print('sfx', len(blob)//1024, 'KiB')
music = json.load(open(os.path.join(os.path.dirname(__file__), 'music.json')))
for i, m in enumerate(music):
    a = load(find(m), 10000)[:10000*60]; d = adpcm(a)
    open(f'{OUT}/m{i}.adp', 'wb').write(d); print(m, len(d)//1024, 'KiB')
# one-shot script sounds, per level
sounds = json.load(open(os.path.join(os.path.dirname(__file__), 'level_sounds.json')))
for lvl, names in sounds.items():
    table = b''; blob = b''; found = 0
    for f in names:
        try: d = adpcm(load(find(f), 8000)[:8000*2])
        except SystemExit: d = b''
        table += struct.pack('<II', len(blob), len(d)); blob += d; found += bool(d)
    open(f'{OUT}/l{lvl}.snd', 'wb').write(struct.pack('<I', len(names))+table+blob)
    print('level', lvl, 'script sounds', found, '/', len(names), len(blob)//1024, 'KiB')
# dialogue: subtitle text and voice lines
FOLD = str.maketrans({'ø': 'o', 'Ø': 'O', 'é': 'e', 'è': 'e', 'ë': 'e', 'ê': 'e', 'á': 'a', 'à': 'a', 'ä': 'a', 'â': 'a', 'ï': 'i', 'í': 'i', 'î': 'i', 'ö': 'o', 'ó': 'o', 'ô': 'o', 'ü': 'u', 'ú': 'u', 'û': 'u', 'ç': 'c', 'ñ': 'n', 'É': 'E', 'È': 'E', 'Ë': 'E', '’': "'", '‘': "'", '“': '"', '”': '"', '…': '...', '–': '-', '—': '-'})
dlg = json.load(open(f'{SRC}/data/dialogue/nl.json'))
voices = {}
for root, _, files in os.walk(f'{SRC}/assets/voices'):
    for f in files: voices[f.lower()] = os.path.join(root, f)
entries = []; texts = bytearray(); total = 0
for key, e in sorted(dlg.items()):
    text = (e.get('text') or '').translate(FOLD).encode('ascii', 'replace').decode()
    vid = -1; v = os.path.basename(e.get('voice') or '').lower()
    if v in voices:
        d = adpcm(load(voices[v], 11025)); vid = len([x for x in entries if x[2] >= 0])
        open(f'{OUT}/v{vid}.adp', 'wb').write(d); total += len(d)
    entries.append((key.lower().encode()[:31], len(texts), vid, int(float(e.get('duration') or 0)*16)))
    texts += text.encode()+b'\0'
tab = b''.join(struct.pack('<32sIhH', k, off, vid, dur) for k, off, vid, dur in entries)
open(f'{OUT}/dlg.bin', 'wb').write(struct.pack('<I', len(entries))+tab+bytes(texts))
print('dialogue', len(entries), 'lines, voices', total//1024, 'KiB')
im = Image.open(f'{SRC}/assets/menu/mainscre.png').convert('RGB').resize((256, 192), Image.BOX)
a = (np.array(im, dtype=np.float32)*.55).astype(np.uint8)
q = Image.fromarray(a).quantize(colors=240, method=Image.MEDIANCUT, dither=Image.NONE)
pal = q.getpalette()[:720]; pal += [0]*(720-len(pal))
def c15(i): return (pal[i*3] >> 3) | ((pal[i*3+1] >> 3) << 5) | ((pal[i*3+2] >> 3) << 10) | 0x8000
open(f'{OUT}/hud.bin', 'wb').write(struct.pack('<240H', *[c15(i) for i in range(240)])+np.array(q, dtype=np.uint8).tobytes())
