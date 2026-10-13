#!/usr/bin/env python3
"""Generate royalty-free demo karaoke files (CDG+MP3 zips and MP4s).

Made-up lyrics and synthesized chords, so the app can be tested (and
screenshotted) without real songs. Needs Python 3 with Pillow and ffmpeg:

    tools/make-demo-songs.py OUTPUT_DIR

The CDG is written packet by packet (300 packets/s, 24 bytes each): it sets
the palette, draws each lyric line as 6x12 tiles and then re-draws the tiles
in the highlight color from left to right while the "song" plays.
"""

import os
import subprocess
import sys
import tempfile
import zipfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "..", "assets", "fonts", "Inter-SemiBold.ttf")

W, H = 300, 216
TILE_W, TILE_H = 6, 12
PPS = 300  # packets per second
DURATION = 32  # seconds

BG, TEXT, HIGHLIGHT, BORDER = 0, 1, 2, 3
PALETTE = {BG: (0x1, 0x1, 0x3), TEXT: (0xF, 0xF, 0xF), HIGHLIGHT: (0xC, 0x3, 0xF), BORDER: (0x1, 0x0, 0x2)}

SONGS = [
    ("DEMO-001", "The Demo Band", "First Song", "cdg"),
    ("DEMO-002", "The Demo Band", "Second Song", "cdg"),
    ("DEMO-003", "Sample Singer", "Summer Night", "mp4"),
    ("DEMO-004", "Sample Singer", "City Lights", "cdg"),
    ("DEMO-005", "Karaoke Stars", "Dancing Together", "cdg"),
    ("DEMO-006", "Karaoke Stars", "Golden Hour", "mp4"),
]

LYRICS = [
    ("This is a PlayKaraoke demo", "sing along with the words"),
    ("the colors change in time", "as the music goes on"),
    ("now is your turn to shine", "take the mic and have fun"),
]


def packet(instruction, data):
    data = bytes(data) + bytes(16 - len(data))
    return bytes([0x09, instruction, 0, 0]) + data + bytes(4)


def memory_preset(color):
    return packet(1, [color, 0])


def border_preset(color):
    return packet(2, [color])


def load_clut():
    data = []
    for i in range(8):
        r, g, b = PALETTE.get(i, (0, 0, 0))
        data += [((r << 2) | (g >> 2)) & 0x3F, (((g & 3) << 4) | b) & 0x3F]
    return packet(30, data)


def tile(row, col, color0, color1, rows):
    return packet(6, [color0, color1, row, col] + rows)


def render_line(text, top, font):
    """1-bit bitmap of a lyric line, centered, with its top at `top` pixels."""
    img = Image.new("1", (W, H), 0)
    draw = ImageDraw.Draw(img)
    width = draw.textlength(text, font=font)
    draw.text(((W - width) / 2, top), text, font=font, fill=1)
    return img


def line_tiles(img, tile_rows):
    """Tiles (row, col, 12 row bytes) with ink, sorted left to right."""
    tiles = []
    for row in tile_rows:
        for col in range(1, W // TILE_W - 1):
            rows = []
            for y in range(TILE_H):
                bits = 0
                for x in range(TILE_W):
                    if img.getpixel((col * TILE_W + x, row * TILE_H + y)):
                        bits |= 1 << (5 - x)
                rows.append(bits)
            if any(rows):
                tiles.append((row, col, rows))
    return sorted(tiles, key=lambda t: (t[1], t[0]))


def make_cdg(title, artist):
    font = ImageFont.truetype(FONT, 17)
    title_font = ImageFont.truetype(FONT, 22)
    timeline = {}  # packet index -> list of packets

    def at(seconds, pkt):
        timeline.setdefault(int(seconds * PPS), []).append(pkt)

    at(0, memory_preset(BG))
    at(0.01, border_preset(BORDER))
    at(0.02, load_clut())

    # Title card.
    for text, top, f in ((title, 70, title_font), (artist, 104, font), ("PlayKaraoke demo", 150, font)):
        for row, col, rows in line_tiles(render_line(text, top, f), range(1, 17)):
            at(0.1, tile(row, col, BG, TEXT, rows))

    t = 4.0
    for first, second in LYRICS:
        at(t, memory_preset(BG))
        lines = [line_tiles(render_line(first, 62, font), range(4, 8)),
                 line_tiles(render_line(second, 122, font), range(9, 13))]
        for tiles in lines:
            for row, col, rows in tiles:
                at(t + 0.05, tile(row, col, BG, TEXT, rows))
        # Highlight sweep, 4 seconds per line.
        for n, tiles in enumerate(lines):
            start = t + 0.5 + n * 4
            for i, (row, col, rows) in enumerate(tiles):
                at(start + 3.6 * i / max(1, len(tiles)), tile(row, col, BG, HIGHLIGHT, rows))
        t += 9.0

    total = DURATION * PPS
    out = bytearray()
    queue = []
    for i in range(total):
        queue.extend(timeline.get(i, []))
        out += queue.pop(0) if queue else bytes(24)
    return bytes(out)


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", *args], check=True)


# Synthesized chords: C - Am - F - G, 2 seconds each.
CHORDS = "aevalsrc='0.12*(sin(2*PI*f1*t)+sin(2*PI*f2*t)+sin(2*PI*f3*t))*(0.6+0.4*sin(2*PI*2*t))':s=44100:d={d}"


def audio_filter():
    parts = []
    notes = [(261.6, 329.6, 392.0), (220.0, 261.6, 329.6), (174.6, 220.0, 261.6), (196.0, 246.9, 293.7)]
    for i in range(DURATION // 2):
        f1, f2, f3 = notes[i % 4]
        parts.append(CHORDS.format(d=2).replace("f1", str(f1)).replace("f2", str(f2)).replace("f3", str(f3)))
    inputs = ";".join(f"{p}[a{i}]" for i, p in enumerate(parts))
    concat = "".join(f"[a{i}]" for i in range(len(parts)))
    return f"{inputs};{concat}concat=n={len(parts)}:v=0:a=1[out]"


def make_mp3(path):
    ffmpeg("-filter_complex", audio_filter(), "-map", "[out]", "-b:a", "128k", path)


def make_mp4(path, title, artist):
    font = os.path.abspath(FONT)
    lines = [f"drawtext=fontfile={font}:text='{title}':fontsize=96:fontcolor=white:x=(w-tw)/2:y=420:enable='lt(t,4)'",
             f"drawtext=fontfile={font}:text='{artist}':fontsize=56:fontcolor=0xC060F0:x=(w-tw)/2:y=560:enable='lt(t,4)'"]
    t = 4
    for first, second in LYRICS:
        for n, text in enumerate((first, second)):
            y = 440 + n * 120
            lines.append(f"drawtext=fontfile={font}:text='{text}':fontsize=72:fontcolor=white:"
                         f"x=(w-tw)/2:y={y}:enable='between(t,{t},{t + 9})'")
            lines.append(f"drawtext=fontfile={font}:text='{text}':fontsize=72:fontcolor=0xC060F0:"
                         f"x=(w-tw)/2:y={y}:enable='between(t,{t + 0.5 + n * 4},{t + 9})'")
        t += 9
    video = f"color=c=0x140a24:s=1920x1080:r=25:d={DURATION}," + ",".join(lines)
    with tempfile.TemporaryDirectory() as tmp:
        mp3 = os.path.join(tmp, "a.mp3")
        make_mp3(mp3)
        ffmpeg("-f", "lavfi", "-i", video, "-i", mp3, "-c:v", "libx264", "-pix_fmt", "yuv420p",
               "-c:a", "aac", "-shortest", path)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "demo-songs"
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        mp3 = os.path.join(tmp, "audio.mp3")
        make_mp3(mp3)
        for code, artist, title, kind in SONGS:
            name = f"{code} - {artist} - {title}"
            if kind == "mp4":
                make_mp4(os.path.join(out_dir, name + ".mp4"), title, artist)
            else:
                with zipfile.ZipFile(os.path.join(out_dir, name + ".zip"), "w") as z:
                    z.writestr(f"{title}.cdg", make_cdg(title, artist))
                    z.write(mp3, f"{title}.mp3")
            print("ok", name)


if __name__ == "__main__":
    main()
