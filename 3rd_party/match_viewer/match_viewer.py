#!/usr/bin/env python3
"""Visor de archivos .match de Nitrogen: dibuja el control (sticks y botones) en el tiempo.

    python match_viewer.py [archivo.match]

Solo libreria estandar (tkinter). Espacio = play/pausa, flechas = +-1 s.
El formato esta definido en include/nitrogen/recordnreplay/format.hpp y codec.hpp;
si cambia alla, cambia aca (parse_match es el unico lugar que lo conoce).
"""

import bisect
import struct
import sys
import time
import tkinter as tk
import zlib
from tkinter import filedialog, messagebox, ttk

HEADER = struct.Struct("<4sHBB8I20sI")  # 64 bytes
BLOCK = struct.Struct("<4sIIIHHI")      # 24 bytes
assert HEADER.size == 64 and BLOCK.size == 24

BUTTONS = ["L1", "L2", "R1", "R2", "UP", "DOWN", "LEFT", "RIGHT", "X", "B", "Y", "A"]  # bit 0..11
CONTROL_CHANNELS = ("lx", "ly", "rx", "ry", "buttons")


class MatchError(Exception):
    pass


def _varint(b, p):
    v = s = 0
    while True:
        c = b[p]
        p += 1
        v |= (c & 0x7F) << s
        if c < 0x80:
            return v, p
        s += 7
        if s > 28:
            raise IndexError("varint demasiado largo")


def _unzigzag(u):
    return (u >> 1) ^ -(u & 1)


def _wrap32(x):
    return ((x + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def parse_match(path):
    """Devuelve un dict: times (us), controllers {nombre: [(lx, ly, rx, ry, botones), ...]},
    channels (todos), frames, duration_us, period_us, bad_blocks, truncated."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 64:
        raise MatchError("el archivo es mas chico que una cabecera")
    magic, version, kind, flags, run_id, period_us, block_bytes, table_bytes, table_count, table_crc, frames, dur, _, crc = HEADER.unpack_from(data)
    if magic != b"NRNR" or crc != zlib.crc32(data[:60]):
        raise MatchError("no es un archivo de Nitrogen (cabecera invalida)")
    if version != 1:
        raise MatchError(f"version {version} no soportada")
    if kind != 1:
        raise MatchError("es un .macro, no un .match")

    # Tabla de canales: 3 strings (objeto, perfil, canal) y exp10, every, flags
    p = 64
    end = p + table_bytes
    if end > len(data) or zlib.crc32(data[p:end]) != table_crc:
        raise MatchError("tabla de canales corrupta")
    chan = []
    try:
        for _ in range(table_count):
            names = []
            for _ in range(3):
                n = data[p]
                names.append(data[p + 1:p + 1 + n].decode("utf-8", "replace"))
                p += 1 + n
            exp10, every, _fl = struct.unpack_from("<bBB", data, p)
            p += 3
            chan.append({"object": names[0], "profile": names[1], "name": names[2], "exp10": exp10, "every": every or 1})
    except (IndexError, struct.error):
        raise MatchError("tabla de canales corrupta")

    # Objetos que tienen los 5 canales del control (en cualquier perfil)
    ctrl_idx = {}
    for obj in dict.fromkeys(c["object"] for c in chan):
        idx = {c["name"]: i for i, c in enumerate(chan) if c["object"] == obj}
        if all(k in idx for k in CONTROL_CHANNELS):
            ctrl_idx[obj] = [idx[k] for k in CONTROL_CHANNELS]
    wanted = [i for v in ctrl_idx.values() for i in v]
    col = {i: j for j, i in enumerate(wanted)}

    n = len(chan)
    every = [c["every"] for c in chan]
    times, rows, bad, truncated = [], [], 0, False
    pos = end
    while pos + BLOCK.size <= len(data):
        bmagic, seq, frame0, t0, nf, plen, bcrc = BLOCK.unpack_from(data, pos)
        if bmagic != b"BLK1":
            truncated = True  # no se puede saber donde empieza el siguiente: se corta aca
            break
        start = pos + BLOCK.size
        pos = start + plen
        if pos > len(data):
            truncated = True
            break
        payload = data[start:pos]
        if zlib.crc32(payload) != bcrc:
            bad += 1
            continue
        t, q, prev = t0, 0, []
        blk_t, blk_r = [], []
        try:
            for i in range(nf):
                dt, q = _varint(payload, q)
                t += dt
                if i == 0:  # keyframe: todos los canales, valor absoluto
                    for _ in range(n):
                        u, q = _varint(payload, q)
                        prev.append(_unzigzag(u))
                else:  # mascara de los canales que "tocan" y delta de los que cambiaron
                    k = frame0 + i
                    due = [c for c in range(n) if k % every[c] == 0]
                    mask = payload[q:q + (len(due) + 7) // 8]
                    q += (len(due) + 7) // 8
                    for bit, c in enumerate(due):
                        if mask[bit >> 3] >> (bit & 7) & 1:
                            u, q = _varint(payload, q)
                            prev[c] = _wrap32(prev[c] + _unzigzag(u))
                blk_t.append(t)
                blk_r.append([prev[c] for c in wanted])
        except IndexError:
            bad += 1
            continue
        times += blk_t
        rows += blk_r

    controllers = {obj: [tuple(r[col[i]] for i in idx) for r in rows] for obj, idx in ctrl_idx.items()}
    return {"times": times, "controllers": controllers, "channels": chan, "frames": len(times),
            "duration_us": times[-1] if times else 0, "period_us": period_us, "clean": bool(flags & 1),
            "bad_blocks": bad, "truncated": truncated, "run_id": run_id}


# ------------------------------------------------------------------------------ interfaz

BG, BODY, OFF, ON, STICK, TEXT = "#16181d", "#2a2e37", "#454b57", "#38d27c", "#e8eaf0", "#c9ceda"
SR = 70  # radio de cada stick


class Viewer(tk.Tk):
    def __init__(self, path=None):
        super().__init__()
        self.title("Nitrogen - visor de .match")
        self.configure(bg=BG)
        self.m = None
        self.t = 0.0  # us
        self.playing = False
        self.last = 0.0
        self.busy = False  # evita que el slider se dispare solo al moverlo por codigo

        bar = tk.Frame(self, bg=BG)
        bar.pack(fill="x", padx=10, pady=(10, 0))
        ttk.Button(bar, text="Abrir...", command=self.open_dialog).pack(side="left")
        self.play_btn = ttk.Button(bar, text="Play", width=7, command=self.toggle)
        self.play_btn.pack(side="left", padx=6)
        self.speed = ttk.Combobox(bar, values=["0.25x", "0.5x", "1x", "2x", "4x"], width=5, state="readonly")
        self.speed.set("1x")
        self.speed.pack(side="left")
        self.ctrl = ttk.Combobox(bar, values=[], width=10, state="readonly")
        self.ctrl.pack(side="left", padx=6)
        self.ctrl.bind("<<ComboboxSelected>>", lambda e: self.draw())
        self.clock = tk.Label(bar, text="0.00 / 0.00 s", bg=BG, fg=TEXT, font=("Consolas", 11))
        self.clock.pack(side="right")

        self.canvas = tk.Canvas(self, width=700, height=420, bg=BG, highlightthickness=0)
        self.canvas.pack(padx=10, pady=6)
        self.slider = ttk.Scale(self, from_=0, to=1, command=self.on_slider)
        self.slider.pack(fill="x", padx=14)
        self.info = tk.Label(self, text="Abri un .match", bg=BG, fg=TEXT, anchor="w", font=("Segoe UI", 9))
        self.info.pack(fill="x", padx=12, pady=(2, 10))

        self.build_controller()
        self.bind("<space>", lambda e: self.toggle())
        self.bind("<Left>", lambda e: self.seek(self.t - 1e6))
        self.bind("<Right>", lambda e: self.seek(self.t + 1e6))
        if path:
            self.load(path)
        self.after(15, self.tick)

    # Dibuja el control una sola vez; despues solo se cambian colores y posiciones
    def build_controller(self):
        c = self.canvas
        c.create_rectangle(40, 70, 660, 400, fill=BODY, outline="")
        self.btn = {}

        def rect(name, x0, y0, x1, y1):
            self.btn[name] = c.create_rectangle(x0, y0, x1, y1, fill=OFF, outline="")
            c.create_text((x0 + x1) / 2, (y0 + y1) / 2, text=name, fill=TEXT, font=("Segoe UI", 9, "bold"))

        def disc(name, x, y):
            self.btn[name] = c.create_oval(x - 17, y - 17, x + 17, y + 17, fill=OFF, outline="")
            c.create_text(x, y, text=name, fill=TEXT, font=("Segoe UI", 10, "bold"))

        rect("L2", 90, 20, 230, 48); rect("L1", 90, 52, 230, 78)
        rect("R2", 470, 20, 610, 48); rect("R1", 470, 52, 610, 78)
        rect("UP", 155, 100, 185, 128); rect("DOWN", 155, 156, 185, 184)
        rect("LEFT", 118, 128, 154, 156); rect("RIGHT", 186, 128, 222, 156)
        disc("X", 530, 108); disc("A", 568, 142); disc("B", 530, 176); disc("Y", 492, 142)

        self.sticks = []
        for cx, cy in ((210, 300), (490, 300)):
            c.create_oval(cx - SR, cy - SR, cx + SR, cy + SR, fill="#20232a", outline=OFF, width=2)
            c.create_line(cx - SR, cy, cx + SR, cy, fill=OFF)
            c.create_line(cx, cy - SR, cx, cy + SR, fill=OFF)
            dot = c.create_oval(cx - 14, cy - 14, cx + 14, cy + 14, fill=STICK, outline="")
            txt = c.create_text(cx, cy + SR + 14, text="", fill=TEXT, font=("Consolas", 10))
            self.sticks.append((cx, cy, dot, txt))

    def open_dialog(self):
        p = filedialog.askopenfilename(title="Abrir .match", filetypes=[("Nitrogen match", "*.match"), ("Todos", "*.*")])
        if p:
            self.load(p)

    def load(self, path):
        try:
            m = parse_match(path)
        except (MatchError, OSError) as e:
            messagebox.showerror("No se pudo abrir", str(e))
            return
        if not m["controllers"]:
            messagebox.showerror("Sin control", "El archivo no tiene un control grabado (attach_controller).")
            return
        self.m, self.t, self.playing = m, 0.0, False
        self.play_btn.config(text="Play")
        names = list(m["controllers"])
        self.ctrl.config(values=names)
        self.ctrl.set(names[0])
        self.busy = True
        self.slider.config(to=max(m["duration_us"], 1))
        self.busy = False
        fps = (m["frames"] - 1) * 1e6 / m["duration_us"] if m["frames"] > 1 and m["duration_us"] else 0
        notes = ""
        if m["bad_blocks"]:
            notes += f" | {m['bad_blocks']} bloque(s) corrupto(s) saltado(s)"
        if m["truncated"] or not m["clean"]:
            notes += " | no se cerro limpio (corte de energia?)"
        self.info.config(text=f"{path.replace(chr(92), '/').split('/')[-1]}: {m['frames']} frames, {m['duration_us'] / 1e6:.2f} s, "
                              f"{fps:.1f} Hz reales (nominal {1e6 / m['period_us']:.0f} Hz){notes}")
        self.draw()

    def toggle(self):
        if not self.m:
            return
        if not self.playing and self.t >= self.m["duration_us"]:
            self.t = 0.0
        self.playing = not self.playing
        self.last = time.monotonic()
        self.play_btn.config(text="Pausa" if self.playing else "Play")

    def seek(self, t):
        if self.m:
            self.t = min(max(t, 0.0), float(self.m["duration_us"]))
            self.draw()

    def on_slider(self, v):
        if not self.busy and self.m:
            self.t = float(v)
            self.draw()

    def tick(self):
        if self.playing and self.m:
            now = time.monotonic()
            self.t += (now - self.last) * 1e6 * float(self.speed.get().rstrip("x"))
            self.last = now
            if self.t >= self.m["duration_us"]:
                self.t = float(self.m["duration_us"])
                self.playing = False
                self.play_btn.config(text="Play")
            self.draw()
        self.after(15, self.tick)

    def draw(self):
        m = self.m
        if not m or not m["times"]:
            return
        i = max(bisect.bisect_right(m["times"], self.t) - 1, 0)  # el ultimo frame que ya ocurrio
        lx, ly, rx, ry, btn = m["controllers"][self.ctrl.get()][i]
        c = self.canvas
        for b, name in enumerate(BUTTONS):
            c.itemconfig(self.btn[name], fill=ON if btn >> b & 1 else OFF)
        for (cx, cy, dot, txt), (x, y), (nx, ny) in zip(self.sticks, ((lx, ly), (rx, ry)), (("LX", "LY"), ("RX", "RY"))):
            px, py = cx + x / 127 * SR, cy - y / 127 * SR  # +Y = arriba
            c.coords(dot, px - 14, py - 14, px + 14, py + 14)
            c.itemconfig(txt, text=f"{nx} {x:4d}   {ny} {y:4d}")
        self.clock.config(text=f"{self.t / 1e6:.2f} / {m['duration_us'] / 1e6:.2f} s   frame {i + 1}/{m['frames']}")
        self.busy = True
        self.slider.set(self.t)
        self.busy = False


if __name__ == "__main__":
    Viewer(sys.argv[1] if len(sys.argv) > 1 else None).mainloop()
