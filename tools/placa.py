#!/usr/bin/env python3
"""
Lectura de los archivos derivados de una revision del PCB, para los scripts
que dibujan mapas de la placa: mapa_cortes.py y mapa_senales.py.

Cada revision vive en hardware/revN/ con los mismos nombres de archivo:
  sdf1-gerbers.zip   Gerbers, drills y FlyingProbeTesting.json
  sdf1.net           netlist Protel

FlyingProbeTesting.json es el archivo que usa el fabricante para el test
electrico con puntas moviles: trae cada pad con su posicion, su tamanio y su
red. Es lo que permite ubicar pines sin adivinar. El .epro2 no sirve para
esto: trae el esquematico, no el ruteo.

No es un lector de Gerber general. Entiende lo que exporta EasyEDA Pro para
esta placa: milimetros en formato 4.5, trazos D01, flashes D03 y regiones
G36/G37. Si cambia el exportador, verificar antes de confiar en un mapa.
"""

import collections
import json
import math
import os
import re
import zipfile

MIL = 0.0254              # el archivo de test viene en milesimas de pulgada


class Revision:
    """Los archivos de una carpeta hardware/revN/."""

    def __init__(self, folder):
        self.folder = folder
        self.zip = zipfile.ZipFile(os.path.join(folder, "sdf1-gerbers.zip"))
        self.probe = json.loads(self.zip.read("FlyingProbeTesting.json"))

    def gerber(self, name):
        return self.zip.read(name).decode()

    def netlist(self):
        with open(os.path.join(self.folder, "sdf1.net"), encoding="utf-8", errors="replace") as f:
            return f.read()


def read_pads(rev):
    """{nombre: pad} desde el archivo de test, con nombres como U5_15 o CON1_7.

    Las filas PADnn_m repiten los pads de los componentes, y hay nombres que
    aparecen dos veces con los mismos datos: esas repeticiones se descartan.
    Pero un mismo nombre tambien puede ser dos pads distintos: los dos
    agujeros de fijacion del conector de cartucho se llaman CON1_999. El
    segundo queda como CON1_999#2.
    Las medidas quedan en mm; sx y sy ya vienen girados si el pad esta a 90.
    """
    pads = {}
    for r in rev.probe["pins"]["rows"]:
        name = r[1]
        if name.startswith("PAD"):
            continue
        if name in pads:
            if all(math.hypot(p["x"] - r[2] * MIL, p["y"] - r[3] * MIL) > 0.01
                   for n, p in pads.items() if n.split("#")[0] == name):
                name = "%s#%d" % (name, sum(1 for n in pads if n.split("#")[0] == name) + 1)
            else:
                continue
        sx, sy, ang = float(r[9] or 0) * MIL, float(r[10] or 0) * MIL, float(r[13] or 0)
        if int(round(ang)) % 180 == 90:
            sx, sy = sy, sx
        layers = {"T", "B"} if r[5] == "DIP" else {r[4]}   # un THT esta en las dos capas
        pads[name] = dict(x=r[2] * MIL, y=r[3] * MIL, side=r[4], kind=r[5], net=r[6],
                          shape=r[8], sx=sx, sy=sy, layers=layers)
    return pads


def read_components(rev):
    """{designador: (x, y)} en mm, del archivo de test."""
    return {r[1]: (r[3] * MIL, r[4] * MIL) for r in rev.probe["components"]["rows"]}


def _apertures(text):
    return {int(m.group(1)): float(m.group(3))
            for m in re.finditer(r"%ADD(\d+)([CR]),([\d.]+)(?:X([\d.]+))?\*%", text)}


def read_tracks(rev):
    """Pistas de cobre como (capa, (x1, y1), (x2, y2), ancho), capa T y despues B.

    Descarta los textos en cobre y los trazos de 0,2 mm o menos, que son del
    texto y no conducen nada que interese.
    """
    segs = []
    for name, layer in (("Gerber_TopLayer.GTL", "T"), ("Gerber_BottomLayer.GBL", "B")):
        text = rev.gerber(name)
        aper = _apertures(text)
        cur = None
        x = y = None
        intext = False
        for line in text.splitlines():
            if "Text Start" in line:
                intext = True
            if "Text End" in line:
                intext = False
            m = re.match(r"(?:G54)?D(\d\d+)\*", line)
            if m:
                cur = int(m.group(1))
                continue
            m = re.match(r"(?:G0?1)?X(-?\d+)Y(-?\d+)D0([123])\*", line)
            if not m:
                continue
            nx, ny, op = int(m.group(1)) / 1e5, int(m.group(2)) / 1e5, m.group(3)
            if op == "1" and not intext and x is not None and cur in aper and aper[cur] > 0.21:
                segs.append((layer, (x, y), (nx, ny), aper[cur]))
            x, y = nx, ny
    return segs


def read_lines(rev, name):
    """Trazos de una capa de dibujo (contorno, serigrafia) como ((x1, y1), (x2, y2), ancho)."""
    text = rev.gerber(name)
    aper = _apertures(text)
    cur = None
    x = y = None
    out = []
    inregion = False
    for line in text.splitlines():
        if line.startswith("G36"):
            inregion = True
        if line.startswith("G37"):
            inregion = False
        m = re.match(r"(?:G54)?D(\d\d+)\*", line)
        if m:
            cur = int(m.group(1))
            continue
        m = re.match(r"(?:G0?1)?X(-?\d+)Y(-?\d+)D0([12])\*", line)
        if not m:
            continue
        nx, ny = int(m.group(1)) / 1e5, int(m.group(2)) / 1e5
        if m.group(3) == "1" and x is not None and not inregion:
            out.append(((x, y), (nx, ny), aper.get(cur, 0.15)))
        x, y = nx, ny
    return out


def read_vias(rev):
    """Centros de los vias, en mm."""
    return [(float(a), float(b))
            for a, b in re.findall(r"X(-?[\d.]+)Y(-?[\d.]+)", rev.gerber("Drill_PTH_Through_Via.DRL"))]


def read_holes(rev):
    """Agujeros metalizados como (x, y, diametro). Incluye los vias."""
    text = rev.gerber("Drill_PTH_Through.DRL")
    tools = dict(re.findall(r"^(T\d+)C([\d.]+)", text, re.M))
    holes = []
    cur = None
    for line in text.splitlines():
        m = re.match(r"^(T\d+)$", line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^X(-?[\d.]+)Y(-?[\d.]+)", line)
        if m:
            holes.append((float(m.group(1)), float(m.group(2)), float(tools[cur])))
    return holes


def bbox(lines):
    """(xmin, xmax, ymin, ymax) de una lista de trazos."""
    xs = [p[0] for line in lines for p in line[:2]]
    ys = [p[1] for line in lines for p in line[:2]]
    return min(xs), max(xs), min(ys), max(ys)


def read_netlist(rev):
    """(tipo de parte por designador, red de cada pin, funcion de cada pin).

    Los pines se nombran como en el archivo de test (U5_15). La funcion es el
    nombre del pin en el simbolo: "Y0" en un '138, "/WAIT" en el conector.
    """
    text = rev.netlist()
    parttype = {}
    for block in re.findall(r"\[(.*?)\]", text, re.S):
        lines = [l.strip() for l in block.strip().splitlines()]
        d = dict(zip(lines[0::2], lines[1::2]))
        if "DESIGNATOR" in d:
            parttype[d["DESIGNATOR"]] = d.get("PARTTYPE", "")
    pin_net = {}
    pin_func = {}
    for block in re.findall(r"\((.*?)\)", text, re.S):
        lines = [l.strip() for l in block.strip().splitlines() if l.strip()]
        if len(lines) < 2:
            continue
        name = lines[0]
        for l in lines[1:]:
            m = re.match(r"(\w+)-(\w+)\s+(.*)$", l)
            if not m:
                continue
            ref, pin, rest = m.group(1), m.group(2), m.group(3)
            body = rest.rsplit(None, 1)[0] if len(rest.split()) > 1 else rest
            part = parttype.get(ref, "")
            if body.startswith(part + "-"):
                body = body[len(part) + 1:]
            pin_net["%s_%s" % (ref, pin)] = name
            pin_func["%s_%s" % (ref, pin)] = body
    return parttype, pin_net, pin_func


def dist_point_segment(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L))
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy))


def _in_pad(p, pad, width):
    dx, dy = abs(p[0] - pad["x"]), abs(p[1] - pad["y"])
    if pad["shape"] == "R":
        return dx <= pad["sx"] / 2 + width / 4 and dy <= pad["sy"] / 2 + width / 4
    return math.hypot(dx, dy) <= max(pad["sx"], pad["sy"]) / 2 + width / 4


def connectivity(segs, pads, vias):
    """Grafo de lo que se toca en el cobre.

    Nodos: ("s", indice de pista), ("p", nombre de pad), ("v", indice de via).
    Dos pistas de la misma capa se tocan si un extremo cae sobre la otra; una
    pista toca un pad si un extremo cae adentro, y un via si un extremo cae en
    su centro. No mira el cruce de dos pistas sin extremo comun: en el Gerber
    de EasyEDA cada union termina en un vertice.
    """
    adj = collections.defaultdict(set)

    def link(a, b):
        adj[a].add(b)
        adj[b].add(a)

    for i, (li, a1, a2, wi) in enumerate(segs):
        for j in range(i + 1, len(segs)):
            lj, b1, b2, wj = segs[j]
            if li != lj:
                continue
            tol = max(wi, wj) / 2
            if min(dist_point_segment(a1, b1, b2), dist_point_segment(a2, b1, b2),
                   dist_point_segment(b1, a1, a2), dist_point_segment(b2, a1, a2)) < tol:
                link(("s", i), ("s", j))
        for n, pad in pads.items():
            if li in pad["layers"] and (_in_pad(a1, pad, wi) or _in_pad(a2, pad, wi)):
                link(("s", i), ("p", n))
        for k, v in enumerate(vias):
            if min(math.hypot(a1[0] - v[0], a1[1] - v[1]), math.hypot(a2[0] - v[0], a2[1] - v[1])) < 0.35:
                link(("s", i), ("v", k))
    return adj


def reach(adj, start, skip=None):
    """Todos los nodos conectados a start, sin pasar por skip."""
    seen = {start}
    stack = [start]
    while stack:
        u = stack.pop()
        for w in adj[u]:
            if w != skip and w not in seen:
                seen.add(w)
                stack.append(w)
    return seen
