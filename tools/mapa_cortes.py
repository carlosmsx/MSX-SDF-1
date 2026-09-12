#!/usr/bin/env python3
"""
Dibuja el mapa de las correcciones de la rev1 (hardware/rev1/CORRECCIONES.md):
donde van los dos cortes y los puntos de soldadura, sobre las pistas reales.

Las pistas salen de las capas de cobre de los Gerbers y los pads, del archivo
de test (ver placa.py). Las dos redes que se tocan no se buscan por nombre: se
recorre el cobre desde un pin conocido.

Los puntos de corte estan fijos en CUTS. Salen de --analizar, que prueba
cortar cada tramo de la red, se queda con los que separan solo la rama buscada
y mide la distancia al cobre vecino a lo largo de cada uno. Si se regeneran los
Gerbers, correr --analizar y revisar CUTS y SOLDER antes de regenerar el mapa.

Uso:
    python tools/mapa_cortes.py hardware/rev1 hardware/rev1/cortes-rev1.svg
    python tools/mapa_cortes.py hardware/rev1 --analizar
"""

import math
import re
import sys

import placa

# Redes que tocan las correcciones: (pin desde el que se recorre, pin que el
# corte tiene que separar, pines que tienen que seguir conectados entre si).
NETS = [
    ("U5_15", "CON1_7", ["U5_15", "U2_19", "U3_23"]),   # /IOSEL: corte 2 saca /WAIT
    ("U5_6", "U3_26", ["U5_6"]),                         # PC3 a G1 de U5: corte 1
]

# Los dos cortes van en la cara inferior. Elegidos con --analizar: el corte 1
# en la mitad de la diagonal que sale del pin 6 de U5 (casi 3 mm libres), el
# corte 2 en la diagonal de /WAIT, a 1,5 mm del codo que baja al contacto 7.
CUTS = [("CORTE 1", (37.47, -52.70)), ("CORTE 2", (76.97, -59.81))]
SOLDER = [
    ("A", 41.27, -40.64, "U5-15 (/IOSEL): a U6 pines 12 y 13"),
    ("B", 41.27, -38.10, "U5-16 (VCC): puente a U5-6"),
    ("C", 33.66, -50.80, "U5-6 (G1): queda aislado por el corte 1"),
    ("D", 97.98, -25.40, "U3-26 (PC3): a U6 pin 9, y 10K a U3-22"),
    ("E", 97.98, -35.56, "U3-22 (GND): otra pata del 10K"),
    ("F", 78.11, -60.96, "lado conector de /WAIT, en el codo: a U6 pin 8"),
]
BG = "#f4f1e8"


def net_items(adj, start):
    """(pistas, pads, vias) de la red que contiene al pin start."""
    R = placa.reach(adj, ("p", start))
    return (sorted(n for t, n in R if t == "s"),
            sorted(n for t, n in R if t == "p"),
            sorted(n for t, n in R if t == "v"))


def clearance(p, layer, width, segs, pads, vias, netsegs, netpads, netvias):
    """Distancia libre, en mm, del punto p de una pista al cobre de otras redes."""
    m = 99
    for j, (lj, c1, c2, wj) in enumerate(segs):
        if lj == layer and j not in netsegs:
            m = min(m, placa.dist_point_segment(p, c1, c2) - wj / 2 - width / 2)
    for n, pd in pads.items():
        if n in netpads or layer not in pd["layers"]:
            continue
        if pd["shape"] == "R":
            d = math.hypot(max(0, abs(p[0] - pd["x"]) - pd["sx"] / 2), max(0, abs(p[1] - pd["y"]) - pd["sy"] / 2))
        else:
            d = math.hypot(p[0] - pd["x"], p[1] - pd["y"]) - max(pd["sx"], pd["sy"]) / 2
        m = min(m, d - width / 2)
    for k, v in enumerate(vias):
        if k not in netvias:
            m = min(m, math.hypot(p[0] - v[0], p[1] - v[1]) - 0.305 - width / 2)
    return m


def analyze(segs, pads, vias, adj):
    for start, isolate, keep in NETS:
        netsegs, netpads, netvias = net_items(adj, start)
        print("mapa_cortes: red de %s, pads %s" % (start, " ".join(netpads)))
        for i in netsegs:
            Rk = placa.reach(adj, ("p", keep[0]), skip=("s", i))
            if ("p", isolate) in Rk or not all(("p", k) in Rk for k in keep):
                continue
            l, a, b, w = segs[i]
            best = max((clearance((a[0] + (b[0] - a[0]) * t / 20, a[1] + (b[1] - a[1]) * t / 20), l, w,
                                  segs, pads, vias, set(netsegs), set(netpads), set(netvias)),
                        (a[0] + (b[0] - a[0]) * t / 20, a[1] + (b[1] - a[1]) * t / 20))
                       for t in range(1, 20))
            print("    corte valido: pista %d, capa %s, %.2f mm; mejor punto (%.2f, %.2f) con %.2f mm libres"
                  % (i, "superior" if l == "T" else "inferior", math.hypot(b[0] - a[0], b[1] - a[1]),
                     best[1][0], best[1][1], best[0]))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    rev = placa.Revision(sys.argv[1])
    segs = placa.read_tracks(rev)
    pads = placa.read_pads(rev)
    vias = placa.read_vias(rev)
    adj = placa.connectivity(segs, pads, vias)

    if sys.argv[2] == "--analizar":
        analyze(segs, pads, vias, adj)
        return

    outline = [(a, b) for a, b, w in placa.read_lines(rev, "Gerber_BoardOutlineLayer.GKO")]
    board = placa.bbox(outline)
    comps = {n: p for n, p in placa.read_components(rev).items() if re.match(r"^(U\d|CON1|H1|X1|R1)$", n)}
    netc = {}
    for i in net_items(adj, NETS[0][0])[0]:
        netc[i] = "#1f9d55"
    for i in net_items(adj, NETS[1][0])[0]:
        netc[i] = "#d9480f"
    clip_n = [0]

    def view(mirror, title, ox, oy, scale, box, pinlabels=(), marksize=1.0):
        X0, X1, Y0, Y1 = box
        W = (X1 - X0) * scale
        H = (Y1 - Y0) * scale

        def P(x, y):
            u = (X1 - x) if mirror else (x - X0)
            return ox + u * scale, oy + (Y1 - y) * scale

        clip_n[0] += 1
        cid = "c%d" % clip_n[0]
        s = ['<g font-family="system-ui,sans-serif">',
             '<text x="%d" y="%d" font-size="18" font-weight="600" fill="#222">%s</text>' % (ox, oy - 10, title),
             '<clipPath id="%s"><rect x="%.1f" y="%.1f" width="%.1f" height="%.1f"/></clipPath>' % (cid, ox, oy, W, H),
             '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" stroke="#999"/>' % (ox, oy, W, H, BG),
             '<g clip-path="url(#%s)">' % cid]
        for a, b in outline:
            (x1, y1), (x2, y2) = P(*a), P(*b)
            s.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#555" stroke-width="2"/>' % (x1, y1, x2, y2))
        # la capa del otro lado va primero, tenue y punteada
        far, near = ("T", "B") if mirror else ("B", "T")
        for layer in (far, near):
            for i, (l, a, b, w) in enumerate(segs):
                if l != layer:
                    continue
                (x1, y1), (x2, y2) = P(*a), P(*b)
                col = netc.get(i, "#b04a4a" if l == "T" else "#3a6ea5")
                wid = max(1.2, w * scale) * (1.5 if i in netc else 1)
                op = 0.95 if i in netc else (0.9 if l == near else 0.22)
                dash = "" if l == near else ' stroke-dasharray="%.0f %.0f"' % (scale * 0.6, scale * 0.45)
                s.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f" stroke-linecap="round" opacity="%.2f"%s/>' % (x1, y1, x2, y2, col, wid, op, dash))
        for n, pd in pads.items():
            if near not in pd["layers"]:
                continue
            cx, cy = P(pd["x"], pd["y"])
            if pd["shape"] == "R":
                s.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#d4a017" opacity="0.55"/>' % (cx - pd["sx"] * scale / 2, cy - pd["sy"] * scale / 2, pd["sx"] * scale, pd["sy"] * scale))
            else:
                s.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#c9a227" stroke="#7a6112" stroke-width="0.8" opacity="0.8"/>' % (cx, cy, max(pd["sx"], pd["sy"]) * scale / 2))
        for v in vias:
            cx, cy = P(*v)
            s.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#777"/>' % (cx, cy, 0.305 * scale))
        fs = 15 * marksize
        for n, (x, y) in comps.items():
            cx, cy = P(x, y)
            s.append('<text x="%.1f" y="%.1f" font-size="%.0f" font-weight="700" fill="#111" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (cx, cy, fs, BG, n))
        for pin, text, dx, dy in pinlabels:
            cx, cy = P(pads[pin]["x"], pads[pin]["y"])
            s.append('<text x="%.1f" y="%.1f" font-size="%.0f" fill="#111" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (cx + dx * marksize, cy + dy * marksize, 12 * marksize, BG, text))
        if mirror:   # los cortes son de la cara inferior: solo en las vistas desde abajo
            for lab, pt in CUTS:
                cx, cy = P(*pt)
                r = 8 * marksize
                s.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#e00" stroke-width="%.1f"/>' % (cx - r, cy - r, cx + r, cy + r, 3.5 * marksize))
                s.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#e00" stroke-width="%.1f"/>' % (cx - r, cy + r, cx + r, cy - r, 3.5 * marksize))
                s.append('<text x="%.1f" y="%.1f" font-size="%.0f" font-weight="700" fill="#e00" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (cx + r + 4, cy - r, 14 * marksize, BG, lab))
        for lab, x, y, txt in SOLDER:
            cx, cy = P(x, y)
            r = 8 * marksize
            s.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="#6f2dbd" stroke-width="%.1f"/>' % (cx, cy, r, 3 * marksize))
            s.append('<text x="%.1f" y="%.1f" font-size="%.0f" font-weight="700" fill="#6f2dbd" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (cx - r - 12 * marksize, cy + 5 * marksize, 14 * marksize, BG, lab))
        s.append("</g></g>")
        return "\n".join(s), W, H

    parts = []
    y = 36
    S = 7.0
    v, W, H = view(False, "Vista desde el lado de componentes", 20, y, S, board,
                   [("U5_1", "U5 pin 1", 30, -8), ("U3_1", "U3 pin 1", 30, -8), ("U2_1", "U2 pin 1", 30, -8)])
    parts.append(v)
    y += H + 60
    v, _, H = view(True, "Vista desde abajo, espejada (placa dada vuelta)", 20, y, S, board,
                   [("U5_1", "U5 pin 1", -30, -8), ("U3_1", "U3 pin 1", -30, -8), ("U2_1", "U2 pin 1", -30, -8)])
    parts.append(v)
    y += H + 60
    TOTW = W + 40

    # detalles ampliados de cada corte, los dos desde abajo
    Z1 = (29.5, 45.5, -58.5, -35.0)
    Z2 = (68.0, 88.0, -71.0, -55.5)
    u5labels = [("U5_%d" % n, str(n), (-14 if n <= 8 else 14), 4) for n in range(1, 17)]
    con_labels = [("CON1_%d" % n, str(n), 0, -30) for n in (1, 3, 5, 7, 9)]
    sc = min((TOTW - 60) / 2 / (Z1[1] - Z1[0]), (TOTW - 60) / 2 / (Z2[1] - Z2[0]))
    v1, W1, H1 = view(True, "Detalle corte 1, desde abajo", 20, y, sc, Z1, u5labels, 1.0)
    v2, W2, H2 = view(True, "Detalle corte 2, desde abajo", 40 + W1, y, sc, Z2,
                      con_labels + [("CON1_7", "contacto 7", 0, -48)], 1.0)
    parts += [v1, v2]
    y += max(H1, H2) + 40

    leg = ['<g font-family="system-ui,sans-serif" font-size="14" fill="#222">',
           '<line x1="20" y1="%d" x2="50" y2="%d" stroke="#d9480f" stroke-width="4"/><text x="58" y="%d">PC3 (U3-26) a U5-6 (MSX_EN_PIN)</text>' % (y, y, y + 5),
           '<line x1="20" y1="%d" x2="50" y2="%d" stroke="#1f9d55" stroke-width="4"/><text x="58" y="%d">/IOSEL: U5-15 a U2-19, U3-23 y CON1-7 (/WAIT)</text>' % (y + 24, y + 24, y + 29),
           '<text x="20" y="%d">Linea llena: capa de este lado. Punteada: capa del otro lado. Gris: via.</text>' % (y + 53),
           '<text x="20" y="%d">X roja: corte (los dos van en la cara inferior). Circulo violeta: punto de soldadura.</text>' % (y + 73)]
    for i, (lab, _x, _y, txt) in enumerate(SOLDER):
        leg.append('<text x="20" y="%d"><tspan font-weight="700" fill="#6f2dbd">%s</tspan>  %s</text>' % (y + 100 + i * 20, lab, txt))
    leg.append('<text x="20" y="%d" fill="#666" font-size="12">Generado desde sdf1-gerbers.zip (pistas) y FlyingProbeTesting.json (pads por red).</text>' % (y + 100 + len(SOLDER) * 20 + 8))
    leg.append("</g>")
    TH = y + 100 + len(SOLDER) * 20 + 24
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="100%%" viewBox="0 0 %.0f %.0f" style="max-width:%.0fpx">' % (TOTW, TH, TOTW)
           + '<rect width="100%" height="100%" fill="#fff"/>' + "\n".join(parts) + "\n".join(leg) + "</svg>")
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(svg)
    print("mapa_cortes: %s" % sys.argv[2])


if __name__ == "__main__":
    main()
