"""
Identificador de pins y copas de Override, con ventana. Dos modos (arriba de la foto):

COLORES  (empieza aqui) dibujas zonas sobre cada color y aprende que es rojo, amarillo, azul, gris de copa
         y FONDO (la mesa u otra cosa que se confunda: lo que marques como fondo deja de contar como color).
  elegir color                   botones o teclas 1-5
  clic, clic, clic...            pone puntos de un poligono; cierra con doble clic, Enter o clic en el primer punto
  arrastrar                      dibujo libre; al soltar se cierra solo
  clic dentro de una zona        la elige; Supr la borra.  Esc cancela el poligono a medias
  "Calcular colores"             aprende la tabla de colores y saca las firmas del sensor
  M                              pinta encima lo que detecta como cada color (para revisar)

OBJETOS  arrastras una caja sobre el pin (y la parte gris de la copa si tiene) y predice que es.
  D / "Detectar pines"           busca solo todos los pines de la foto
  Enter / "Correcto"             la prediccion estaba bien
  1-9 / botones                  la etiqueta correcta (aprende)
  clic en una caja / Supr        elegir / borrar

En los dos: flechas izquierda/derecha cambian de foto. Todo se guarda solo (ejemplos.json, colores.json y
colores_lut.npy, junto a este archivo) y sigue ahi la proxima vez.

Uso:
  python identificar_pines.py                 (te pregunta la carpeta)
  python identificar_pines.py carpeta/        (todos los .png de la carpeta)
  python identificar_pines.py --selftest
"""
import argparse
import base64
import json
import os
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog

import cv2
import numpy as np

# Rangos HSV de OpenCV (H 0-179, S y V 0-255). Solo mientras no hay colores aprendidos.
COLORES = {
    "rojo": [((0, 90, 60), (8, 255, 255)), ((170, 90, 60), (179, 255, 255))],
    "amarillo": [((18, 90, 80), (35, 255, 255))],
    "azul": [((95, 90, 40), (130, 255, 255))],
}
GRIS = ((0, 0, 50), (179, 45, 210))  # parte gris opaca de la copa
ORDEN = ["rojo", "amarillo", "azul"]
CLASES = ORDEN + ["gris", "fondo"]
NOMBRE = {"gris": "gris de copa", "fondo": "fondo / mesa (excluir)"}
PINTA = {"rojo": "#ff3030", "amarillo": "#ffd000", "azul": "#2f7bff", "gris": "#c0c0c0", "fondo": "#b060ff"}
PINTA_BGR = {"rojo": (48, 48, 255), "amarillo": (0, 208, 255), "azul": (255, 123, 47), "gris": (192, 192, 192)}
SIG_ID = {"rojo": 1, "azul": 2, "amarillo": 3}  # mismos ids que override_vision.cpp
RANGO_SENSOR = 2.5  # el range de las firmas (el del Vision Utility)
FIRMA_MIN = 200     # medio ancho minimo de una firma en u y en v
# Los unicos pines que existen: siempre dos colores (en cualquier orden: amarillo-rojo girado es rojo-amarillo)
COMBOS = {("amarillo", "amarillo"), ("amarillo", "azul"), ("amarillo", "rojo"), ("azul", "rojo")}
# shortcut: umbral adivinado; el fondo gris del campo tambien cuenta, asi que conviene marcar cajas justas
# y corregir: con ejemplos manda el kNN
GRIS_COPA = 0.12  # fraccion de la seleccion que es gris (fuera del pin) desde la que se cuenta copa
# Nombres fijos (asi el kNN no aprende "pin azul-amarillo" y "pin amarillo azul" como cosas distintas)
PINES = [f"pin {a}-{b}" for a, b in sorted(COMBOS)]
ETIQUETAS = PINES + [p + " con copa" for p in PINES] + ["copa sola"]
N_FEATS = 14
AQUI = os.path.dirname(os.path.abspath(__file__))
EJEMPLOS = os.path.join(AQUI, "ejemplos.json")
COLORES_JSON = os.path.join(AQUI, "colores.json")
LUT_NPY = os.path.join(AQUI, "colores_lut.npy")

# Tabla de colores aprendida (como la LUT del Vision Sensor): cada celda de HSV dice de que clase es.
BH, BS, BV = 4, 8, 8                           # tamano de celda en H, S, V
NH, NS, NV = 180 // BH, 256 // BS, 256 // BV
NADA = 255                                     # celda sin clase
LUT_MIN = 2e-5  # densidad minima (suavizada) para que una celda cuente; mas alto = mas estricto
LUT = None      # (NH, NS, NV) uint8 con el indice en CLASES, o None mientras no se aprenda


# ---------------------------------------------------------------- colores

def celdas(bgr):
    hsv = cv2.cvtColor(bgr.reshape(-1, 1, 3).astype(np.uint8), cv2.COLOR_BGR2HSV).reshape(-1, 3)
    return hsv[:, 0] // BH, hsv[:, 1] // BS, hsv[:, 2] // BV


def clases_de(bgr, lut):
    return lut[celdas(bgr)].reshape(bgr.shape[:-1])


def mascaras(bgr):
    if LUT is not None:
        cls = clases_de(bgr, LUT)
        return {c: cls == CLASES.index(c) for c in ORDEN}
    hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
    out = {}
    for nombre, rangos in COLORES.items():
        m = np.zeros(hsv.shape[:2], bool)
        for lo, hi in rangos:
            m |= cv2.inRange(hsv, np.array(lo), np.array(hi)) > 0
        out[nombre] = m
    return out


def mascara_gris(bgr, color):
    if LUT is not None:
        return clases_de(bgr, LUT) == CLASES.index("gris")
    hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
    return (cv2.inRange(hsv, np.array(GRIS[0]), np.array(GRIS[1])) > 0) & ~color


def suavizar(c):
    """Promedio con las celdas vecinas: cubre tonos parecidos que no salieron en las zonas. El hue da la vuelta."""
    c = (c + np.roll(c, 1, axis=1) + np.roll(c, -1, axis=1)) / 3
    for ax in (2, 3):
        d = c.copy()
        sl = [slice(None)] * 4
        sl[ax] = slice(1, None)
        sl2 = list(sl)
        sl2[ax] = slice(None, -1)
        d[tuple(sl)] += c[tuple(sl2)]
        d[tuple(sl2)] += c[tuple(sl)]
        c = d / 3
    return c


def entrenar_lut(px, peso_fondo=1.0):
    """px: {clase: pixeles BGR (N,3)}. Cada celda HSV queda de la clase que mas pixeles marcados tiene ahi
    (cada clase pesa igual, aunque marques mucho mas fondo). peso_fondo > 1 hace que el fondo gane los empates:
    excluye mas mesa a cambio de perder algo del color parecido."""
    cnt = np.zeros((len(CLASES), NH, NS, NV), np.float32)
    for k, c in enumerate(CLASES):
        p = px.get(c)
        if p is None or len(p) < 30:
            continue
        np.add.at(cnt[k], celdas(p), 1)
        cnt[k] /= len(p)
    for _ in range(2):
        cnt = suavizar(cnt)
    cnt[CLASES.index("fondo")] *= peso_fondo
    return np.where(cnt.max(axis=0) >= LUT_MIN, cnt.argmax(axis=0), NADA).astype(np.uint8)


def poner_lut(lut):
    global LUT
    LUT = lut


# ---------------------------------------------------------------- firmas del sensor

def uv(px):
    """u, v como el Vision Sensor (Pixy): (R-G) y (B-G) divididos por R+G+B, escalados a 32768.
    Los pixeles muy oscuros se ignoran, como en el sensor."""
    px = px.astype(np.int64)
    b, g, r = px[:, 0], px[:, 1], px[:, 2]
    s = r + g + b
    ok = s >= 0.1 * 765
    return (r - g)[ok] * 32768 // s[ok], (b - g)[ok] * 32768 // s[ok]


def firma_px(px, q=20):
    """Firma estilo Vision Utility: bordes en los percentiles q y 100-q (q=20 es lo del Utility), centro = medio.
    Aproximada: la camara de la foto no ve los colores igual que el sensor."""
    u, v = uv(px)
    if len(u) < 20:
        return None
    umin, umax = (int(np.percentile(u, p)) for p in (q, 100 - q))
    vmin, vmax = (int(np.percentile(v, p)) for p in (q, 100 - q))
    um, vm = (umin + umax) // 2, (vmin + vmax) // 2
    # un color muy parejo daria ancho ~0 y el sensor no detectaria nada: medio ancho minimo
    umin, umax = min(umin, um - FIRMA_MIN), max(umax, um + FIRMA_MIN)
    vmin, vmax = min(vmin, vm - FIRMA_MIN), max(vmax, vm + FIRMA_MIN)
    return umin, umax, um, vmin, vmax, vm


def dentro_firma(px, f, rango=RANGO_SENSOR):
    """Fraccion de pixeles que el sensor contaria dentro de la firma: caja estirada desde el centro por el range."""
    u, v = uv(px)
    if not len(u):
        return 0.0
    umin, umax, um, vmin, vmax, vm = f
    ok = (u >= um + (umin - um) * rango) & (u <= um + (umax - um) * rango) & \
         (v >= vm + (vmin - vm) * rango) & (v <= vm + (vmax - vm) * rango)
    return float(ok.mean())


def ajustar_firma(px, otros):
    """El sensor solo entiende cajas: prueba cajas mas anchas y mas angostas y se queda con la que cubre mas
    de su color castigando fuerte lo que se le cuela de los otros (fondo incluido)."""
    mejor = None
    for q in (5, 10, 15, 20, 25, 30, 35, 40):
        f = firma_px(px, q)
        if f is None:
            return None, 0.0, {}
        cubre = dentro_firma(px, f)
        cuela = {o: dentro_firma(p, f) for o, p in otros.items()}
        puntaje = cubre - 3 * max(cuela.values(), default=0)
        if mejor is None or puntaje > mejor[0]:
            mejor = (puntaje, f, cubre, cuela)
    return mejor[1:]


def calcular_colores(px, peso_fondo=1.0):
    """px: {clase: pixeles BGR (N,3)} -> tabla aprendida + por clase como se clasifican sus pixeles y su firma."""
    lut = entrenar_lut(px, peso_fondo)
    validos = {c: p for c, p in px.items() if len(p) >= 30}
    res = {"lut": lut}
    for c, p in validos.items():
        cls = lut[celdas(p)]
        r = {"n": int(len(p)), "como": {o: float((cls == CLASES.index(o)).mean()) for o in CLASES}}
        r["como"]["nada"] = float((cls == NADA).mean())
        if c in SIG_ID:
            r["firma"], r["cubre_firma"], r["cuela"] = ajustar_firma(p, {o: q for o, q in validos.items() if o != c})
        res[c] = r
    return res


def informe(res):
    pct = lambda x: f"{100 * x:.0f}%"
    out = []
    for c in CLASES:
        if c not in res:
            out.append(f"{NOMBRE.get(c, c).upper()}: sin zonas" + ("" if c == "fondo" else " (marca al menos una)"))
            out.append("")
            continue
        r = res[c]
        out.append(f"{NOMBRE.get(c, c).upper()}  ({r['n']} px)")
        otros = {o: v for o, v in r["como"].items() if o != c and v >= 0.005}
        conf = ", ".join(f"{o} {pct(v)}" for o, v in sorted(otros.items(), key=lambda kv: -kv[1]))
        out.append(f"  foto: lo reconoce {pct(r['como'][c])} | lo toma como: {conf or '-'}"
                   + ("   <-- revisar" if r["como"][c] < 0.9 else ""))
        f = r.get("firma")
        if f:
            umin, umax, um, vmin, vmax, vm = f
            out.append(f"  sensor: pros::Vision::signature_from_utility({SIG_ID[c]}, {umin}, {umax}, {um}, "
                       f"{vmin}, {vmax}, {vm}, {RANGO_SENSOR}, 0);")
            out.append(f"        robot: u centro {um}  u ancho {(umax - umin) // 2}  v centro {vm}  "
                       f"v ancho {(vmax - vmin) // 2}  rango {RANGO_SENSOR}")
            conf = ", ".join(f"{o} {pct(v)}" for o, v in r["cuela"].items())
            malo = any(v > 0.05 for v in r["cuela"].values())
            out.append(f"        cubre {pct(r['cubre_firma'])} | se le cuela: {conf or '-'}"
                       + ("   <-- el sensor lo va a mezclar" if malo else ""))
        if c == "gris":
            out.append("        (el sensor ve mal el gris: casi no tiene color)")
        out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------- objetos

def analizar(roi):
    """Pin (objeto de color mas grande) y gris de copa en la seleccion: colores de arriba a abajo y rasgos."""
    m = mascaras(roi)
    color = m["rojo"] | m["amarillo"] | m["azul"]
    H, W = roi.shape[:2]
    gris = mascara_gris(roi, color)
    gris_frac = float(gris.sum() / (H * W))
    # Gris por tercio de la seleccion (arriba, medio, abajo): donde esta la copa respecto del pin
    feats_gris = [float(gris[k * H // 3:(k + 1) * H // 3].mean()) if H >= 3 else 0.0 for k in range(3)]
    base = {"gris": gris_frac, "mascaras": m}
    if color.sum() < 50:
        if gris_frac < GRIS_COPA:
            return None
        return {**base, "segs": [], "aspecto": H / max(W, 1),
                "feats": [float(np.log(H / max(W, 1))), 0.0] + [0.0] * 9 + feats_gris}
    cerrado = cv2.morphologyEx(color.astype(np.uint8), cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    n, lab, stats, _ = cv2.connectedComponentsWithStats(cerrado)
    idx = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    x, y, w, h = (int(v) for v in stats[idx, :4])
    obj = (lab == idx)[y:y + h, x:x + w]
    por_color = [(m[c][y:y + h, x:x + w] & obj) for c in ORDEN]

    # Color dominante de cada fila, luego tramos; los tramos muy cortos (bordes, reflejos) se descartan
    cuenta = np.stack([pc.sum(axis=1) for pc in por_color], axis=1)  # (h, 3)
    valida = cuenta.sum(axis=1) >= max(2, 0.15 * w)
    dom = np.where(valida, cuenta.argmax(axis=1), -1)
    tramos = []  # [color, filas]
    for d in dom:
        if d < 0:
            continue
        if tramos and tramos[-1][0] == d:
            tramos[-1][1] += 1
        else:
            tramos.append([int(d), 1])
    minimo = max(3, 0.12 * h)
    segs = []
    for d, filas in tramos:
        if filas >= minimo and (not segs or segs[-1] != ORDEN[d]):
            segs.append(ORDEN[d])
    if not segs:  # todo era corto: se queda con el color que mas hay
        segs = [ORDEN[int(cuenta.sum(axis=0).argmax())]]

    # Rasgos: forma + que fraccion de cada color hay en el tercio de arriba, medio y abajo
    feats = [float(np.log(h / max(w, 1))), float(obj.sum() / (w * h))]
    for k in range(3):
        a, b = k * h // 3, (k + 1) * h // 3
        tot = sum(int(pc[a:b].sum()) for pc in por_color) or 1
        feats += [int(pc[a:b].sum()) / tot for pc in por_color]
    return {**base, "segs": segs, "feats": feats + feats_gris, "aspecto": h / max(w, 1)}


def detectar_pines(img):
    """Cajas de los pines de toda la foto: manchas de rojo/amarillo/azul (las dos mitades de un pin se juntan),
    agrandadas un poco para que entre el gris de la copa si tiene."""
    m = mascaras(img)
    color = (m["rojo"] | m["amarillo"] | m["azul"]).astype(np.uint8)
    H, W = img.shape[:2]
    k = max(3, min(H, W) // 100)
    color = cv2.morphologyEx(color, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))  # puntos sueltos fuera
    color = cv2.morphologyEx(color, cv2.MORPH_CLOSE, np.ones((k, k), np.uint8))  # une las mitades del pin
    n, lab, stats, _ = cv2.connectedComponentsWithStats(color)
    # shortcut: tamano minimo fijo (0.05% de la foto); si salen manchitas falsas o faltan pines lejanos, ajustar
    minimo = max(80, H * W * 0.0005)
    cajas = []
    for i in range(1, n):
        x, y, w, h, area = (int(v) for v in stats[i])
        if area < minimo:
            continue
        mg = int(0.25 * max(w, h))
        x0, y0 = max(0, x - mg), max(0, y - mg)
        cajas.append((x0, y0, min(W, x + w + mg) - x0, min(H, y + h + mg) - y0))
    return cajas


def iou(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    iw = max(0, min(ax + aw, bx + bw) - max(ax, bx))
    ih = max(0, min(ay + ah, by + bh) - max(ay, by))
    inter = iw * ih
    return inter / float(aw * ah + bw * bh - inter or 1)


def regla(info):
    segs = info["segs"]
    copa = " con copa" if info["gris"] >= GRIS_COPA else ""
    if not segs:
        return "copa sola"
    if len(segs) == 1:
        c = segs[0]
        if c == "amarillo":  # no hay pines de un color: amarillo parejo es amarillo-amarillo
            return "pin amarillo-amarillo" + copa
        return f"pin ?-{c}{copa} (solo se ve {c})"  # la otra mitad tapada: que lo decida el kNN
    par = tuple(sorted((segs[0], segs[-1])))
    txt = f"pin {par[0]}-{par[1]}{copa}"
    return txt if par in COMBOS else txt + " (combinacion NO valida)"


def knn(feats, ejemplos, k=3):
    """Vota entre los k ejemplos mas parecidos; empate: gana el mas cercano."""
    x = np.array(feats)
    dist = sorted((float(np.linalg.norm(x - np.array(e["feats"]))), e["etiqueta"]) for e in ejemplos)[:k]
    votos = {}
    for _, et in dist:
        votos[et] = votos.get(et, 0) + 1
    mejor = max(votos, key=votos.get)
    return mejor if votos[mejor] > 1 else dist[0][1]


def confirmados(data, excepto=None):  # los de otra version (otro largo de rasgos) no se usan para aprender
    return [e for e in data if e is not excepto and e.get("confirmado", True) and len(e.get("feats", [])) == N_FEATS]


def predecir(info, data):
    conf = confirmados(data, info)
    if len(conf) >= 3:
        return knn(info["feats"], conf), f"aprendida de {len(conf)} ejemplos (la regla dice: {regla(info)})"
    return regla(info), "por color y forma; corrigela para que aprenda"


# ---------------------------------------------------------------- archivos

def leer_json(ruta, vacio):
    try:
        with open(ruta, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return vacio


def escribir_json(ruta, data):
    tmp = ruta + ".tmp"  # se escribe aparte y se reemplaza: un corte no deja el archivo a medias
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    os.replace(tmp, ruta)


def guardar(data):
    escribir_json(EJEMPLOS, data)


def clave(ruta):
    return os.path.normcase(os.path.abspath(ruta))


def exportar_robot(aprendidos, ruta):
    """vision.txt para la raiz de la SD del robot (override_vision.cpp lo carga al encender).
    Solo firmas: la exposicion y la calibracion de distancia se quedan las que tenga el robot... salvo que este
    archivo reemplace al suyo, en cuyo caso hay que volver a calibrar la distancia (boton A)."""
    lineas = []
    for c, sid in SIG_ID.items():
        f = (aprendidos.get(c) or {}).get("firma")
        if not f:
            continue
        umin, umax, um, vmin, vmax, vm = f
        # el robot usa cajas simetricas (centro +- medio ancho): se toma el lado mas ancho para no perder color
        lineas.append(f"sig {sid} {um} {max(umax - um, um - umin)} {vm} {max(vmax - vm, vm - vmin)} {RANGO_SENSOR:.2f}")
    with open(ruta, "w", encoding="ascii", newline="\n") as fh:
        fh.write("\n".join(lineas) + "\n")
    return len(lineas)


def pixeles_de_zonas(zonas):
    """Junta los pixeles BGR de todas las zonas de cada clase, en todas las fotos."""
    px = {c: [] for c in CLASES}
    cache = {}
    for z in zonas:
        if z["img"] not in cache:
            cache[z["img"]] = cv2.imread(z["img"])
        img = cache[z["img"]]
        if img is None or z["color"] not in px:
            continue
        m = np.zeros(img.shape[:2], np.uint8)
        cv2.fillPoly(m, [np.array(z["poly"], np.int32)], 1)
        px[z["color"]].append(img[m > 0])
    return {c: np.concatenate(v) if v else np.zeros((0, 3), np.uint8) for c, v in px.items()}


# ---------------------------------------------------------------- ventana

class App:
    VERDE, NARANJA = "#33cc55", "#ff9922"

    def __init__(self, root, rutas):
        self.root, self.rutas, self.i = root, rutas, 0
        self.data = leer_json(EJEMPLOS, [])
        self.col = leer_json(COLORES_JSON, {"zonas": [], "aprendidos": {}})
        try:
            lut = np.load(LUT_NPY)
            poner_lut(lut if lut.shape == (NH, NS, NV) else None)
        except (OSError, ValueError):
            poner_lut(None)
        self.sel = self.zsel = None
        self.img = None
        self.esc, self.ox, self.oy = 1.0, 0, 0
        self.drag = self.goma = None
        self.poly, self.trazo, self.eligio = [], 0, False
        self.color_act = "rojo"
        self.ver_mascara = False
        self.version = 0  # sube al recalcular colores: invalida la imagen cacheada
        self.foto_key = None
        root.title("Identificar pines")
        root.geometry("1340x800")

        izq = tk.Frame(root)
        izq.pack(side="left", fill="both", expand=True)
        nav = tk.Frame(izq, pady=4)
        nav.pack(fill="x")
        tk.Button(nav, text="◀  Anterior", width=12, command=lambda: self.ir(-1)).pack(side="left", padx=6)
        tk.Button(nav, text="Siguiente  ▶", width=12, command=lambda: self.ir(1)).pack(side="right", padx=6)
        self.modo = tk.StringVar(value="colores" if LUT is None else "objetos")
        for m, txt in (("colores", "1. Colores"), ("objetos", "2. Objetos")):
            tk.Radiobutton(nav, text=txt, value=m, variable=self.modo, indicatoron=False, width=12,
                           font=("Segoe UI", 10, "bold"), command=self.cambiar_modo).pack(side="left", padx=2)
        self.titulo = tk.Label(nav, font=("Segoe UI", 11, "bold"))
        self.titulo.pack()
        self.canvas = tk.Canvas(izq, bg="#1e1e1e", highlightthickness=0, cursor="crosshair")
        self.canvas.pack(fill="both", expand=True)
        self.ayuda = tk.Label(izq, fg="#777")
        self.ayuda.pack(fill="x")

        self.der_obj = self.panel_objetos(root)
        self.der_col = self.panel_colores(root)

        c = self.canvas
        c.bind("<ButtonPress-1>", lambda e: self.c_presionar(e) if self.es_col() else self.presionar(e))
        c.bind("<B1-Motion>", lambda e: self.c_arrastrar(e) if self.es_col() else self.arrastrar(e))
        c.bind("<ButtonRelease-1>", lambda e: self.c_soltar(e) if self.es_col() else self.soltar(e))
        c.bind("<Double-Button-1>", lambda e: self.cerrar_poly() if self.es_col() else None)
        c.bind("<Configure>", lambda e: self.dibujar())
        root.bind("<Left>", lambda e: self.ir(-1))
        root.bind("<Right>", lambda e: self.ir(1))
        root.bind("<Return>", lambda e: self.cerrar_poly() if self.es_col() else self.confirmar())
        root.bind("<Delete>", lambda e: self.borrar_zona() if self.es_col() else self.borrar())
        root.bind("<Escape>", lambda e: self.cancelar())
        root.bind("m", lambda e: self.alternar_mascara())
        root.bind("d", lambda e: None if self.es_col() else self.detectar())
        for n in range(1, 10):
            root.bind(str(n), lambda e, n=n: self.tecla_num(n))
        self.cambiar_modo()
        self.cargar()

    # --- paneles
    def panel_objetos(self, root):
        der = tk.Frame(root, width=330, padx=10, pady=8)
        der.pack_propagate(False)
        tk.Button(der, text="🔍  Detectar pines en esta foto   (D)", font=("Segoe UI", 10, "bold"),
                  command=self.detectar).pack(fill="x", pady=(0, 10))
        self.pred = tk.Label(der, font=("Segoe UI", 16, "bold"), wraplength=310, justify="left", anchor="w")
        self.pred.pack(fill="x")
        self.estado = tk.Label(der, fg="#777", wraplength=310, justify="left", anchor="w")
        self.estado.pack(fill="x")
        tk.Button(der, text="✓  Correcto   (Enter)", bg=self.VERDE, fg="white", font=("Segoe UI", 11, "bold"),
                  command=self.confirmar).pack(fill="x", pady=(10, 6))
        tk.Label(der, text="Si no, elige la correcta:", anchor="w").pack(fill="x")
        self.botones = {}
        for n, et in enumerate(ETIQUETAS, 1):
            b = tk.Button(der, text=f"{n}   {et}", anchor="w", command=lambda e=et: self.etiquetar(e))
            b.pack(fill="x", pady=1)
            self.botones[et] = b
        tk.Button(der, text="Otra...", anchor="w", command=self.otra).pack(fill="x", pady=1)
        self.bg_normal = next(iter(self.botones.values())).cget("bg")
        tk.Button(der, text="Borrar caja   (Supr)", command=self.borrar).pack(fill="x", pady=(10, 0))
        self.detalle = tk.Text(der, height=6, wrap="word", font=("Consolas", 8), relief="flat", bg="#f4f4f4")
        self.detalle.pack(fill="both", expand=True, pady=(10, 4))
        self.aprendido = tk.Label(der, fg="#777", anchor="w")
        self.aprendido.pack(fill="x")
        return der

    def panel_colores(self, root):
        der = tk.Frame(root, width=330, padx=10, pady=8)
        der.pack_propagate(False)
        tk.Label(der, text="Que vas a marcar:", anchor="w", font=("Segoe UI", 10, "bold")).pack(fill="x")
        self.bot_color = {}
        for n, c in enumerate(CLASES, 1):
            b = tk.Button(der, text=f"{n}   {NOMBRE.get(c, c)}", anchor="w", font=("Segoe UI", 11),
                          command=lambda c=c: self.elegir_color(c))
            b.pack(fill="x", pady=1)
            self.bot_color[c] = b
        tk.Label(der, fg="#555", wraplength=310, justify="left", anchor="w", text=(
            "Encierra solo ese color, con la forma que tenga (sirve inclinado):\n"
            "· clic, clic, clic... y doble clic (o Enter) para cerrar\n"
            "· o arrastra dibujando libre; al soltar se cierra\n"
            "Marca varias zonas, en varias fotos y luces. Lo que se confunda (la mesa) marcalo como fondo. "
            "Esc cancela.")).pack(fill="x", pady=(8, 4))
        tk.Button(der, text="Borrar zona elegida   (Supr)", command=self.borrar_zona).pack(fill="x", pady=1)
        tk.Button(der, text="Ver lo que detecta   (M)", command=self.alternar_mascara).pack(fill="x", pady=1)
        self.peso_fondo = tk.DoubleVar(value=self.col.get("peso_fondo", 1.0))
        tk.Scale(der, variable=self.peso_fondo, from_=1.0, to=3.0, resolution=0.1, orient="horizontal",
                 label="Excluir fondo:  suave  →  fuerte (pierde algo de color)").pack(fill="x", pady=(8, 0))
        tk.Button(der, text="Calcular colores", bg=self.VERDE, fg="white", font=("Segoe UI", 11, "bold"),
                  command=self.calcular).pack(fill="x", pady=(4, 4))
        tk.Button(der, text="Exportar para el robot (vision.txt)", command=self.exportar).pack(fill="x", pady=(0, 4))
        self.zonas_info = tk.Label(der, fg="#777", anchor="w", justify="left", wraplength=310)
        self.zonas_info.pack(fill="x")
        self.reporte = tk.Text(der, wrap="word", font=("Consolas", 8), relief="flat", bg="#f4f4f4")
        self.reporte.pack(fill="both", expand=True, pady=(6, 0))
        if LUT is not None:
            self.reporte.insert("1.0", "Colores aprendidos cargados. Recalcula si agregas o borras zonas.")
        return der

    def es_col(self):
        return self.modo.get() == "colores"

    def cambiar_modo(self):
        self.cancelar()
        if self.es_col():
            self.der_obj.pack_forget()
            self.der_col.pack(side="right", fill="y")
            self.ayuda.config(text="Clic para poner puntos (doble clic cierra) o arrastra libre  ·  clic dentro de "
                                   "una zona para elegirla  ·  M muestra lo detectado  ·  flechas cambian de foto")
        else:
            self.der_col.pack_forget()
            self.der_obj.pack(side="right", fill="y")
            self.ayuda.config(text="D detecta los pines solo  ·  o arrastra una caja sobre el pin (y el gris de la "
                                   "copa si tiene)  ·  clic en una caja para elegirla  ·  flechas cambian de foto")
        self.dibujar()

    def tecla_num(self, n):
        if self.es_col():
            if n <= len(CLASES):
                self.elegir_color(CLASES[n - 1])
        elif n <= len(ETIQUETAS):
            self.etiquetar(ETIQUETAS[n - 1])

    def cancelar(self):
        self.poly, self.trazo = [], 0
        self.sel = self.zsel = None
        self.dibujar()

    # --- foto actual
    def cajas(self):
        k = clave(self.rutas[self.i])
        return [e for e in self.data if e.get("img") == k]

    def zonas(self):
        k = clave(self.rutas[self.i])
        return [z for z in self.col["zonas"] if z["img"] == k]

    def repredecir(self):  # lo no confirmado se vuelve a predecir con lo ultimo aprendido
        for e in self.cajas():
            if not e.get("confirmado"):
                e["etiqueta"], e["origen"] = predecir(e, self.data)

    def cargar(self):
        self.img = cv2.imread(self.rutas[self.i])
        self.poly, self.sel, self.zsel = [], None, None
        self.repredecir()
        self.dibujar()

    def ir(self, d):
        j = min(max(self.i + d, 0), len(self.rutas) - 1)
        if j != self.i:
            self.i = j
            self.cargar()

    # --- dibujo
    def a_canvas(self, x, y):
        return self.ox + x * self.esc, self.oy + y * self.esc

    def a_imagen(self, x, y):
        h, w = self.img.shape[:2]
        return min(max((x - self.ox) / self.esc, 0), w - 1), min(max((y - self.oy) / self.esc, 0), h - 1)

    def dibujar(self):
        if not hasattr(self, "bot_color"):
            return
        c = self.canvas
        self.titulo.config(text=f"{self.i + 1} / {len(self.rutas)}    {os.path.basename(self.rutas[self.i])}")
        if self.img is None:
            c.delete("all")
            self.foto_key = None
            c.create_text(20, 20, anchor="nw", fill="white", text="No se pudo abrir esta imagen")
            self.paneles()
            return
        cw, ch = max(c.winfo_width(), 200), max(c.winfo_height(), 200)
        h, w = self.img.shape[:2]
        self.esc = min(cw / w, ch / h)
        nw, nh = max(1, int(w * self.esc)), max(1, int(h * self.esc))
        self.ox, self.oy = (cw - nw) // 2, (ch - nh) // 2
        key = (self.i, nw, nh, self.ver_mascara, self.version)
        if key != self.foto_key:  # la foto solo se re-encoda si cambio algo
            vista = cv2.resize(self.img, (nw, nh), interpolation=cv2.INTER_AREA)
            if self.ver_mascara:
                m = mascaras(vista)
                m["gris"] = mascara_gris(vista, m["rojo"] | m["amarillo"] | m["azul"])
                pintada = (vista // 3).astype(np.uint8)  # lo que no es ningun color queda oscuro
                for col, mk in m.items():
                    pintada[mk] = PINTA_BGR[col]
                vista = cv2.addWeighted(vista, 0.25, pintada, 0.75, 0)
            self.foto = tk.PhotoImage(data=base64.b64encode(cv2.imencode(".png", vista)[1].tobytes()))
            self.foto_key = key
        c.delete("all")
        c.create_image(self.ox, self.oy, anchor="nw", image=self.foto)
        self.capas()
        self.paneles()

    def capas(self):
        c = self.canvas
        c.delete("capa")
        if self.img is None:
            return
        if self.es_col():
            for z in self.zonas():
                pts = [v for p in z["poly"] for v in self.a_canvas(*p)]
                if z is self.zsel:
                    c.create_polygon(pts, outline="white", fill="", width=5, tags="capa")
                c.create_polygon(pts, outline=PINTA[z["color"]], fill="", width=2, tags="capa")
                x0, y0 = self.a_canvas(*z["poly"][0])
                c.create_text(x0, y0 - 2, anchor="sw", text=z["color"], fill=PINTA[z["color"]],
                              font=("Segoe UI", 9, "bold"), tags="capa")
            if self.poly:
                pts = [v for p in self.poly for v in self.a_canvas(*p)]
                if len(self.poly) > 1:
                    c.create_line(pts, fill=PINTA[self.color_act], width=2, tags="capa")
                x0, y0 = self.a_canvas(*self.poly[0])
                c.create_oval(x0 - 6, y0 - 6, x0 + 6, y0 + 6, outline="white", width=2, tags="capa")
            return
        for e in self.cajas():
            x, y, bw, bh = e["box"]
            x0, y0 = self.a_canvas(x, y)
            x1, y1 = self.a_canvas(x + bw, y + bh)
            col = self.VERDE if e.get("confirmado") else self.NARANJA
            if e is self.sel:
                c.create_rectangle(x0 - 3, y0 - 3, x1 + 3, y1 + 3, outline="white", width=2, tags="capa")
            c.create_rectangle(x0, y0, x1, y1, outline=col, width=3, dash=() if e.get("confirmado") else (6, 3),
                               tags="capa")
            txt = e["etiqueta"] + ("  ✓" if e.get("confirmado") else "  ?")
            t = c.create_text(x0 + 4, y0 - 4, anchor="sw", text=txt, fill="black", font=("Segoe UI", 10, "bold"),
                              tags="capa")
            c.tag_lower(c.create_rectangle(c.bbox(t), fill=col, outline=col, tags="capa"), t)

    def paneles(self):
        if self.es_col():
            for col, b in self.bot_color.items():
                b.config(bg=PINTA[col] if col == self.color_act else self.bg_normal)
            cuenta = {c: sum(1 for z in self.col["zonas"] if z["color"] == c) for c in CLASES}
            self.zonas_info.config(text="zonas: " + "  ".join(f"{c} {n}" for c, n in cuenta.items()))
            return
        for b in self.botones.values():
            b.config(bg=self.bg_normal)
        self.detalle.delete("1.0", "end")
        conf = len(confirmados(self.data))
        pend = sum(1 for e in self.cajas() if not e.get("confirmado"))
        self.aprendido.config(text=f"{conf} ejemplos aprendidos  ·  {len(self.cajas())} cajas en esta foto"
                                   + (f" ({pend} por confirmar)" if pend else ""))
        e = self.sel
        if e is None:
            self.pred.config(text="Marca un objeto", fg="black")
            self.estado.config(text="D para detectar todos, o arrastra sobre un pin (y el gris de su copa si tiene).")
            return
        ok = e.get("confirmado")
        self.pred.config(text=e["etiqueta"], fg=self.VERDE if ok else self.NARANJA)
        self.estado.config(text="confirmada por ti" if ok else "prediccion " + e.get("origen", ""))
        if e["etiqueta"] in self.botones:
            self.botones[e["etiqueta"]].config(bg=self.VERDE if ok else self.NARANJA)
        lineas = [f"colores de arriba a abajo: {' / '.join(e['segs']) or 'ninguno'}",
                  f"gris de copa: {100 * e.get('gris', 0):.0f}% de la caja (copa desde {100 * GRIS_COPA:.0f}%)",
                  f"alto/ancho: {e['aspecto']:.2f}"]
        self.detalle.insert("1.0", "\n".join(lineas))

    # --- modo colores: mouse
    def c_presionar(self, ev):
        if self.img is None:
            return
        p = self.a_imagen(ev.x, ev.y)
        self.eligio = False
        if not self.poly:  # clic dentro de una zona existente: elegirla
            for z in reversed(self.zonas()):
                if cv2.pointPolygonTest(np.array(z["poly"], np.float32).reshape(-1, 1, 2), p, False) >= 0:
                    self.zsel, self.eligio = z, True
                    self.capas()
                    return
            self.zsel = None
        if len(self.poly) >= 3:
            x0, y0 = self.a_canvas(*self.poly[0])
            if (ev.x - x0) ** 2 + (ev.y - y0) ** 2 <= 100:  # clic en el primer punto: cerrar
                self.cerrar_poly()
                return
        self.poly.append(p)
        self.trazo = 0
        self.capas()

    def c_arrastrar(self, ev):
        if self.eligio or not self.poly or self.img is None:
            return
        lx, ly = self.a_canvas(*self.poly[-1])
        if (ev.x - lx) ** 2 + (ev.y - ly) ** 2 >= 25:
            self.poly.append(self.a_imagen(ev.x, ev.y))
            self.trazo += 1
            self.capas()

    def c_soltar(self, ev):
        if self.trazo >= 3:  # fue dibujo libre (un clic quieto no agrega puntos): se cierra al soltar
            self.cerrar_poly()
        self.trazo = 0

    def cerrar_poly(self):
        pts = [[int(round(x)), int(round(y))] for x, y in self.poly]
        self.poly, self.trazo = [], 0
        if len(pts) >= 3 and cv2.contourArea(np.array(pts, np.int32)) >= 9:
            z = {"img": clave(self.rutas[self.i]), "color": self.color_act, "poly": pts}
            self.col["zonas"].append(z)
            escribir_json(COLORES_JSON, self.col)
        self.dibujar()

    def elegir_color(self, c):
        self.color_act = c
        self.dibujar()

    def borrar_zona(self):
        if self.zsel is not None:
            self.col["zonas"].remove(self.zsel)
            self.zsel = None
            escribir_json(COLORES_JSON, self.col)
            self.dibujar()

    def alternar_mascara(self):
        self.ver_mascara = not self.ver_mascara
        self.dibujar()

    def calcular(self):
        self.reporte.delete("1.0", "end")
        self.reporte.insert("1.0", "calculando...")
        self.root.update_idletasks()
        self.col["peso_fondo"] = round(self.peso_fondo.get(), 1)
        res = calcular_colores(pixeles_de_zonas(self.col["zonas"]), self.col["peso_fondo"])
        poner_lut(res.pop("lut"))
        np.save(LUT_NPY, LUT)
        self.col["aprendidos"] = {c: {"firma": r.get("firma")} for c, r in res.items()}
        escribir_json(COLORES_JSON, self.col)
        self.version += 1
        self.reporte.delete("1.0", "end")
        self.reporte.insert("1.0", informe(res))
        self.dibujar()

    def exportar(self):
        ruta = os.path.join(AQUI, "vision.txt")
        n = exportar_robot(self.col.get("aprendidos", {}), ruta)
        if n == 0:
            messagebox.showwarning("Exportar", "Primero calcula los colores.")
            return
        messagebox.showinfo("Exportar", f"{n} firmas guardadas en:\n{ruta}\n\nCopialo a la raiz de la microSD del "
                                        "robot (como vision.txt). Despues calibra la distancia en el robot (boton A).")

    # --- modo objetos: mouse
    def presionar(self, ev):
        self.drag = (ev.x, ev.y)

    def arrastrar(self, ev):
        if not self.drag:
            return
        if self.goma:
            self.canvas.delete(self.goma)
        self.goma = self.canvas.create_rectangle(*self.drag, ev.x, ev.y, outline="white", dash=(4, 2), width=2)

    def soltar(self, ev):
        if not self.drag or self.img is None:
            return
        (x0, y0), self.drag = self.drag, None
        if self.goma:
            self.canvas.delete(self.goma)
            self.goma = None
        a = ((min(x0, ev.x) - self.ox) / self.esc, (min(y0, ev.y) - self.oy) / self.esc)
        b = ((max(x0, ev.x) - self.ox) / self.esc, (max(y0, ev.y) - self.oy) / self.esc)
        if abs(ev.x - x0) < 6 and abs(ev.y - y0) < 6:  # clic: elegir la caja mas chica bajo el mouse
            bajo = [e for e in self.cajas() if e["box"][0] <= a[0] <= e["box"][0] + e["box"][2]
                    and e["box"][1] <= a[1] <= e["box"][1] + e["box"][3]]
            self.elegir(min(bajo, key=lambda e: e["box"][2] * e["box"][3]) if bajo else None)
            return
        h, w = self.img.shape[:2]
        x, y = int(max(0, a[0])), int(max(0, a[1]))
        self.nueva_caja(x, y, int(min(w, b[0])) - x, int(min(h, b[1])) - y)

    def nueva_caja(self, x, y, w, h, elegir=True):
        if w < 4 or h < 4:
            return None
        info = analizar(self.img[y:y + h, x:x + w])
        if info is None:
            self.estado.config(text="Ahi no hay rojo, amarillo, azul ni gris de copa.")
            return None
        e = {"img": clave(self.rutas[self.i]), "box": [x, y, w, h], "feats": info["feats"], "segs": info["segs"],
             "aspecto": info["aspecto"], "gris": info["gris"], "confirmado": False}
        e["etiqueta"], e["origen"] = predecir(e, self.data)
        self.data.append(e)
        if elegir:
            guardar(self.data)
            self.elegir(e)
        return e

    def detectar(self):
        if self.img is None:
            return
        nuevas = 0
        for caja in detectar_pines(self.img):
            if any(iou(caja, e["box"]) > 0.3 for e in self.cajas()):  # ya habia una caja ahi
                continue
            if self.nueva_caja(*caja, elegir=False):
                nuevas += 1
        guardar(self.data)
        self.sel = None
        self.dibujar()
        self.estado.config(text=f"{nuevas} pines nuevos (naranja = por confirmar). Clic en cada uno y Enter "
                                "si esta bien, o el numero correcto. Supr borra lo que no sea pin.")

    # --- modo objetos: acciones
    def elegir(self, e):
        self.sel = e
        self.dibujar()

    def etiquetar(self, et):
        if self.sel is None:
            self.estado.config(text="Primero marca o elige una caja.")
            return
        self.sel["etiqueta"], self.sel["confirmado"] = et, True
        self.sel.pop("origen", None)
        self.repredecir()
        guardar(self.data)
        pend = [e for e in self.cajas() if not e.get("confirmado")]
        self.sel = pend[0] if pend else self.sel  # salta solo a la siguiente por confirmar
        self.dibujar()

    def confirmar(self):
        if self.sel is not None:
            self.etiquetar(self.sel["etiqueta"])

    def otra(self):
        if self.sel is None:
            return
        et = simpledialog.askstring("Otra etiqueta", "Que es?", parent=self.root)
        if et and et.strip():
            self.etiquetar(et.strip())

    def borrar(self):
        if self.sel is not None:
            self.data.remove(self.sel)
            pend = [e for e in self.cajas() if not e.get("confirmado")]
            self.sel = pend[0] if pend else None
            guardar(self.data)
            self.dibujar()


# ---------------------------------------------------------------- entrada

def expandir(args):
    rutas = []
    for r in args:
        if os.path.isdir(r):  # carpeta: todos sus .png, en orden alfabetico
            rutas += [os.path.join(r, f) for f in sorted(os.listdir(r)) if f.lower().endswith(".png")]
        else:
            rutas.append(r)
    return rutas


def selftest():
    img = np.zeros((200, 200, 3), np.uint8)
    img[20:90, 40:60] = (0, 220, 255)    # amarillo arriba
    img[90:170, 40:60] = (255, 80, 0)    # azul abajo
    img[30:110, 130:150] = (0, 0, 230)   # rojo arriba
    img[110:180, 130:150] = (255, 80, 0)  # azul abajo
    img[150:195, 115:165] = (128, 128, 128)  # gris de copa alrededor de la base
    combinado = analizar(img[:, :100])
    assert combinado["segs"] == ["amarillo", "azul"], combinado["segs"]
    assert len(combinado["feats"]) == N_FEATS
    assert regla(combinado) == "pin amarillo-azul", regla(combinado)
    con_copa = analizar(img[20:200, 105:175])
    assert regla(con_copa) == "pin azul-rojo con copa", regla(con_copa)
    assert regla(analizar(img[150:195, 115:165])) == "copa sola"
    amarillo = analizar(img[20:90, :100])
    assert regla(amarillo) == "pin amarillo-amarillo", regla(amarillo)
    assert all(regla(i) in ETIQUETAS for i in (combinado, con_copa, amarillo))
    assert knn([0, 0], [{"feats": [0, 0], "etiqueta": "a"}, {"feats": [5, 5], "etiqueta": "b"}]) == "a"

    # Detectar solo: dos pines separados -> dos cajas, cada una sobre su pin
    cajas = detectar_pines(img)
    assert len(cajas) == 2, cajas
    assert any(c[0] <= 40 and c[0] + c[2] >= 60 for c in cajas) and any(c[0] <= 130 <= c[0] + c[2] for c in cajas)

    # Colores aprendidos: una mesa beige muy cerca del amarillo. Sin fondo marcado la toma como amarillo;
    # marcandola como fondo deja de serlo y el amarillo sigue reconocido.
    rng = np.random.default_rng(1)
    ruido = lambda base, n=600: np.clip(np.array(base, int) + rng.integers(-12, 13, (n, 3)), 0, 255).astype(np.uint8)
    amar, mesa = ruido((20, 200, 235)), ruido((40, 185, 225))  # mesa: un amarillo un poco mas apagado
    rojo, azul, gris = ruido((30, 30, 220)), ruido((230, 90, 10)), ruido((128, 128, 128))
    base = {"rojo": rojo, "amarillo": amar, "azul": azul, "gris": gris}
    lut_sin = entrenar_lut(base)
    lut_con = entrenar_lut({**base, "fondo": mesa})
    ia, fo = CLASES.index("amarillo"), CLASES.index("fondo")
    sin = (lut_sin[celdas(mesa)] == ia).mean()
    con = (lut_con[celdas(mesa)] == ia).mean()
    assert con < 0.05 and sin > 0.5, (sin, con)
    assert (lut_con[celdas(amar)] == ia).mean() > 0.9
    assert (lut_con[celdas(mesa)] == fo).mean() > 0.9
    res = calcular_colores({**base, "fondo": mesa})
    for c in ("rojo", "amarillo", "azul"):
        assert res[c]["como"][c] > 0.9, (c, res[c]["como"])
    # la firma del sensor es una caja: con colores tan cercanos no puede excluir todo, pero ajustada deja
    # pasar menos fondo que la del Vision Utility (q=20)
    utility = dentro_firma(mesa, firma_px(amar, 20))
    assert res["amarillo"]["cuela"]["fondo"] <= utility, (res["amarillo"]["cuela"], utility)
    assert res["rojo"]["cuela"]["fondo"] < 0.05 and res["azul"]["cuela"]["fondo"] < 0.05
    assert "FONDO" in informe(res)
    print("selftest OK")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Identifica pins y copas de Override en fotos.")
    ap.add_argument("fotos", nargs="*", help="carpetas o .png; sin nada te pregunta la carpeta")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        sys.exit(0)
    root = tk.Tk()
    args = a.fotos
    if not args:
        root.withdraw()
        carpeta = filedialog.askdirectory(title="Carpeta con las fotos (.png)")
        if not carpeta:
            sys.exit(0)
        args = [carpeta]
        root.deiconify()
    rutas = expandir(args)
    if not rutas:
        messagebox.showerror("Identificar pines", "No encontre ningun .png ahi.")
        sys.exit(1)
    App(root, rutas)
    root.mainloop()
