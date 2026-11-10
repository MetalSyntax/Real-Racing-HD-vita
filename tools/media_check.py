#!/usr/bin/env python3
"""Audio/video inventory of the staged bundle: every format must be one the loader decodes on the Vita
(source/coreaudio.c): WAV = 16-bit PCM (AudioFile -> OpenAL), MP3 = MPEG-1/2 layer III (AudioFile ->
libmpg123 -> AudioQueue). Videos (.m4v/.mp4/.mov) are listed: Real Racing HD 1.04 ships none.
Exit 1 if something would not play.

Uso: python3 -I tools/media_check.py [ruta/game_app]
"""
import os, struct, sys

app = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'game_app')
bad, counts = [], {}
for root, _, files in os.walk(app):
    for f in files:
        if f.startswith('._'):
            continue
        ext = os.path.splitext(f)[1].lower()
        p = os.path.join(root, f)
        if ext == '.wav':
            d = open(p, 'rb').read(64)
            pos, ok = 12, False
            while pos + 8 <= len(d):
                cid, n = d[pos:pos + 4], struct.unpack('<I', d[pos + 4:pos + 8])[0]
                if cid == b'fmt ':
                    tag, ch, rate = struct.unpack('<HHI', d[pos + 8:pos + 16])
                    bits = struct.unpack('<H', d[pos + 22:pos + 24])[0]
                    ok = d[:4] == b'RIFF' and tag == 1 and bits == 16
                    break
                pos += 8 + n
            counts['wav'] = counts.get('wav', 0) + 1
            if not ok:
                bad.append(p)
        elif ext == '.mp3':
            d = open(p, 'rb').read(16)
            if d[:3] == b'ID3':  # skip the ID3v2 tag to the first frame header
                size = (d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9]
                with open(p, 'rb') as fh:
                    fh.seek(10 + size)
                    d = fh.read(4)
            counts['mp3'] = counts.get('mp3', 0) + 1
            if not (d[0] == 0xFF and (d[1] & 0xE6) == 0xE2):  # frame sync + layer III
                bad.append(p)
        elif ext in ('.m4v', '.mp4', '.mov', '.caf', '.m4a', '.aac', '.aif', '.aiff'):
            counts[ext[1:]] = counts.get(ext[1:], 0) + 1
            bad.append(p)  # no player/decoder wired for these in this port
print('[media] ' + ', '.join('%s: %d' % kv for kv in sorted(counts.items())) + '; videos: %d' %
      sum(counts.get(k, 0) for k in ('m4v', 'mp4', 'mov')))
for p in bad:
    print('[media] UNSUPPORTED', p)
sys.exit(1 if bad else 0)
