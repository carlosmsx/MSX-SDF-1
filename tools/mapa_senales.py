#!/usr/bin/env python3
"""
Dibuja la placa completa, sin pistas, con la senial que llega a cada pad de
los integrados y de H1. Sirve para usar la placa sin montar los integrados:
cada pad de un zocalo vacio es un punto de acceso a esa senial.

Las seniales salen del netlist: una red que toca el conector CON1 es una senial
del bus del MSX y lleva el nombre del pin del conector. Los pads salen del
archivo de test; el contorno, la serigrafia y los agujeros, de los Gerbers
(ver placa.py). Tambien lista las seniales del conector que no llegan a ningun
pad, y avisa si algun nombre del netlist no coincide con el pinout estandar.

Uso:
    python tools/mapa_senales.py hardware/rev1 hardware/rev1/senales-rev1.svg
"""

import math
import re
import sys

import placa

# pinout estandar del conector de cartucho MSX
STD = {1: "/CS1", 2: "/CS2", 3: "/CS12", 4: "/SLTSL", 5: "(reservado)", 6: "/RFSH", 7: "/WAIT", 8: "/INT", 9: "/M1", 10: "/BUSDIR",
       11: "/IORQ", 12: "/MREQ", 13: "/WR", 14: "/RD", 15: "/RESET", 16: "(reservado)", 17: "A9", 18: "A15", 19: "A11", 20: "A10",
       21: "A7", 22: "A6", 23: "A12", 24: "A8", 25: "A14", 26: "A13", 27: "A1", 28: "A0", 29: "A3", 30: "A2", 31: "A5", 32: "A4",
       33: "D1", 34: "D0", 35: "D3", 36: "D2", 37: "D5", 38: "D4", 39: "D7", 40: "D6", 41: "GND", 42: "CLOCK", 43: "GND",
       44: "SW1", 45: "+5V", 46: "SW2", 47: "+5V", 48: "+12V", 49: "SOUNDIN", 50: "-12V"}

# pines del '138, para rotular los que no van a ningun lado
PIN138 = {1: "A", 2: "B", 3: "C", 4: "/G2A", 5: "/G2B", 6: "G1", 7: "Y7", 8: "GND", 9: "Y6", 10: "Y5", 11: "Y4", 12: "Y3", 13: "Y2", 14: "Y1", 15: "Y0", 16: "VCC"}

COL = {"addr": "#1d5fbf", "data": "#15803d", "ctrl": "#c2410c", "vcc": "#c01c28", "gnd": "#333333", "mcu": "#7b3fb0", "free": "#8a8a8a"}
TITLES = {"U1": "U1 · W27C512 (ROM)", "U2": "U2 · 74LS245", "U3": "U3 · ATmega328P", "U4": "U4 · 74LS138", "U5": "U5 · 74LS138"}
BG = "#f4f1e8"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    rev = placa.Revision(sys.argv[1])
    parttype, pin_net, pin_func = placa.read_netlist(rev)
    msx_nets = {net for p, net in pin_net.items() if p.startswith("CON1_")}

    # los nombres del netlist tienen que coincidir con la tabla estandar
    for p, net in pin_net.items():
        if p.startswith("CON1_"):
            n = int(p.split("_")[1])
            f = pin_func[p].replace("~{", "/").replace("}", "").split("@")[0]
            if f.lstrip("/") != STD[n].replace("+5V", "5V").lstrip("/"):
                print("mapa_senales: AVISO pin %d: netlist %r, estandar %r" % (n, f, STD[n]))
    unrouted = [n for n in STD if "CON1_%d" % n not in pin_net and "reservado" not in STD[n]]

    def classify(pad):
        """(rotulo, categoria) de un pad."""
        ref, pin = pad.split("_")
        net = pin_net.get(pad)
        if net is None:
            if parttype.get(ref, "").startswith("SN74LS138"):
                return PIN138[int(pin)], "free"
            return "sin conexion", "free"
        if net == "VCC":
            return "+5V", "vcc"
        if net == "GND":
            return "GND", "gnd"
        if net in msx_nets:
            if net == "MSX_CS_PIN":
                return "/IOSEL", "ctrl"
            if net == "M1":
                return "/M1", "ctrl"       # el simbolo del conector no le pone la barra
            if re.fullmatch(r"A\d+", net):
                return net, "addr"
            if re.fullmatch(r"D\d", net):
                return net, "data"
            return net, "ctrl"
        if net == "MSX_EN_PIN":
            return "EN", "mcu"
        if net == "$1N104":
            return ("Y0→U5" if ref == "U4" else "←U4"), "mcu"
        if net == "$1N128":
            return "RESET", "mcu"
        if net in ("$1N121", "$1N122"):
            return "XTAL", "mcu"
        if net == "$1N131":
            return "AREF", "mcu"
        return net, "mcu"

    pads = placa.read_pads(rev)
    outline = placa.read_lines(rev, "Gerber_BoardOutlineLayer.GKO")
    silk = placa.read_lines(rev, "Gerber_TopSilkscreenLayer.GTO")
    holes = placa.read_holes(rev)
    X0, X1, Y0, Y1 = placa.bbox(outline)

    S = 12.0
    OX = 20
    OY = 70

    def P(x, y):
        return OX + (x - X0) * S, OY + (Y1 - y) * S

    W = (X1 - X0) * S
    H = (Y1 - Y0) * S
    svg = []
    svg.append('<text x="%d" y="30" font-size="22" font-weight="700" fill="#222">SDF-1 rev1: qué señal llega a cada pad</text>' % OX)
    svg.append('<text x="%d" y="52" font-size="14" fill="#555">Vista desde el lado de componentes, sin pistas. Con los zócalos vacíos, cada pad es un punto de acceso a esa señal.</text>' % OX)
    svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"/>' % (OX, OY, W, H, BG))
    for a, b, w in outline:
        (x1, y1), (x2, y2) = P(*a), P(*b)
        svg.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#555" stroke-width="2"/>' % (x1, y1, x2, y2))
    for a, b, w in silk:
        (x1, y1), (x2, y2) = P(*a), P(*b)
        svg.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#9aa0a6" stroke-width="%.1f" stroke-linecap="round" opacity="0.45"/>' % (x1, y1, x2, y2, max(0.8, w * S)))

    # agujeros: los que no son pad de un componente ni via, son islas
    padded = [(p["x"], p["y"]) for n, p in pads.items() if p["kind"] == "DIP"]
    free_holes = 0
    for hx, hy, d in holes:
        if d < 0.5:     # vias: el archivo PTH tambien los trae
            continue
        if any(math.hypot(hx - px, hy - py) < 0.3 for px, py in padded):
            continue
        cx, cy = P(hx, hy)
        free_holes += 1
        svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="#c9a227" stroke-width="%.1f" opacity="0.7"/>' % (cx, cy, (d / 2 + 0.3) * S, 0.35 * S))

    # conector y pasivos SMD de la cara de componentes. Los contactos del borde
    # son SMD; los dos pads DIP de CON1 son los agujeros de fijacion
    for n, p in pads.items():
        if n.startswith("CON1_") and p["kind"] == "SMD" and p["side"] == "T":
            cx, cy = P(p["x"], p["y"])
            svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#d4a017" opacity="0.6"/>' % (cx - p["sx"] * S / 2, cy - p["sy"] * S / 2, p["sx"] * S, p["sy"] * S))
        elif n.startswith("CON1_") and p["kind"] == "DIP":
            cx, cy = P(p["x"], p["y"])
            svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="#c9a227" stroke-width="%.1f" opacity="0.8"/>' % (cx, cy, max(p["sx"], p["sy"]) * S / 2, 0.35 * S))
        elif p["kind"] == "SMD" and p["side"] == "T" and not n.startswith("CON1_"):
            cx, cy = P(p["x"], p["y"])
            svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#c9a227" opacity="0.5"/>' % (cx - p["sx"] * S / 2, cy - p["sy"] * S / 2, p["sx"] * S, p["sy"] * S))
    for pin in (2, 50):
        p = pads["CON1_%d" % pin]
        cx, cy = P(p["x"], p["y"])
        svg.append('<text x="%.1f" y="%.1f" font-size="12" fill="#222" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">%d</text>' % (cx, cy - p["sy"] * S / 2 - 6, BG, pin))
    c2 = pads["CON1_2"]
    c50 = pads["CON1_50"]
    mx, my = P((c2["x"] + c50["x"]) / 2, c2["y"])
    svg.append('<text x="%.1f" y="%.1f" font-size="13" font-weight="700" fill="#222" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">CON1: contactos pares de este lado (2 a 50); los impares, del otro</text>' % (mx, my + 4, BG))

    # integrados: los rotulos van adentro, entre las dos filas, que con el
    # zocalo vacio es lugar libre; el numero de pin, del lado de afuera
    for ref in ("U1", "U2", "U3", "U4", "U5"):
        mine = {n: p for n, p in pads.items() if n.startswith(ref + "_")}
        xs_ = [p["x"] for p in mine.values()]
        cxm = (min(xs_) + max(xs_)) / 2
        top = max(p["y"] for p in mine.values())
        bot = min(p["y"] for p in mine.values())
        (bx1, by1), (bx2, by2) = P(min(xs_), top), P(max(xs_), bot)
        svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="6" fill="none" stroke="#b8b0a0" stroke-width="1.2" stroke-dasharray="6 4"/>' % (bx1 - 10, by1 - 16, bx2 - bx1 + 20, by2 - by1 + 32))
        tx, ty = P(cxm, top)
        svg.append('<text x="%.1f" y="%.1f" font-size="13" font-weight="700" fill="#111" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (tx, ty - 22, BG, TITLES[ref]))
        for n, p in sorted(mine.items(), key=lambda kv: int(kv[0].split("_")[1])):
            cx, cy = P(p["x"], p["y"])
            r = max(p["sx"], p["sy"]) * S / 2
            label, cat = classify(n)
            col = COL[cat]
            if p["shape"] == "R":
                svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" stroke="#5c4a10" stroke-width="1"/>' % (cx - r, cy - r, 2 * r, 2 * r, "#e0b43a"))
            else:
                svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#e0b43a" stroke="#5c4a10" stroke-width="1"/>' % (cx, cy, r))
            svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s"/>' % (cx, cy, r * 0.45, col))
            left = p["x"] < cxm
            pin = n.split("_")[1]
            weight = "700" if cat in ("addr", "data", "ctrl", "vcc", "gnd") else "400"
            style = ' font-style="italic"' if cat == "free" else ""
            if left:
                svg.append('<text x="%.1f" y="%.1f" font-size="10.5" font-weight="%s"%s fill="%s">%s</text>' % (cx + r + 3, cy + 3.8, weight, style, col, label))
                svg.append('<text x="%.1f" y="%.1f" font-size="8" fill="#777" text-anchor="end">%s</text>' % (cx - r - 2, cy + 3, pin))
            else:
                svg.append('<text x="%.1f" y="%.1f" font-size="10.5" font-weight="%s"%s fill="%s" text-anchor="end">%s</text>' % (cx - r - 3, cy + 3.8, weight, style, col, label))
                svg.append('<text x="%.1f" y="%.1f" font-size="8" fill="#777">%s</text>' % (cx + r + 2, cy + 3, pin))

    # H1: la senial abajo del pad, el numero arriba
    h1 = {n: p for n, p in pads.items() if n.startswith("H1_")}
    for n, p in h1.items():
        cx, cy = P(p["x"], p["y"])
        r = max(p["sx"], p["sy"]) * S / 2
        label, cat = classify(n)
        col = COL[cat]
        if p["shape"] == "R":
            svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#e0b43a" stroke="#5c4a10" stroke-width="1"/>' % (cx - r, cy - r, 2 * r, 2 * r))
        else:
            svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#e0b43a" stroke="#5c4a10" stroke-width="1"/>' % (cx, cy, r))
        svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s"/>' % (cx, cy, r * 0.45, col))
        svg.append('<text x="%.1f" y="%.1f" font-size="11" font-weight="700" fill="%s" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (cx, cy + r + 13, col, BG, label))
        svg.append('<text x="%.1f" y="%.1f" font-size="8" fill="#777" text-anchor="middle">%s</text>' % (cx, cy - r - 3, n.split("_")[1]))
    hx = [p["x"] for p in h1.values()]
    hy = max(p["y"] for p in h1.values())
    tx, ty = P(min(hx) - 2.6, hy)
    svg.append('<text x="%.1f" y="%.1f" font-size="13" font-weight="700" fill="#111" text-anchor="end" stroke="%s" stroke-width="3" paint-order="stroke">H1</text>' % (tx, ty + 5, BG))
    for ref, lab in (("X1", "X1 · 20 MHz"), ("C9", "C9")):
        mine = [p for n, p in pads.items() if n.startswith(ref + "_")]
        for p in mine:
            cx, cy = P(p["x"], p["y"])
            r = max(p["sx"], p["sy"]) * S / 2
            svg.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#e0b43a" stroke="#5c4a10" stroke-width="1"/>' % (cx, cy, r))
        if mine and ref == "X1":
            cx, cy = P(sum(p["x"] for p in mine) / len(mine), max(p["y"] for p in mine))
            svg.append('<text x="%.1f" y="%.1f" font-size="11" fill="#111" text-anchor="middle" stroke="%s" stroke-width="3" paint-order="stroke">%s</text>' % (cx, cy - 12, BG, lab))

    # leyenda
    y = OY + H + 36
    L = ['<g font-family="system-ui,sans-serif" font-size="14" fill="#222">']
    items = [("addr", "Direcciones A0 a A15"), ("data", "Datos D0 a D7"), ("ctrl", "Control del bus del MSX"), ("vcc", "+5 V"), ("gnd", "GND"),
             ("mcu", "Internas (ATmega, H1, cristal, U4 a U5)"), ("free", "Sin conexión en el PCB (itálica)")]
    for i, (cat, text) in enumerate(items):
        row, colx = divmod(i, 4)
        lx = OX + colx * 300
        ly = y + row * 24
        L.append('<circle cx="%d" cy="%d" r="6" fill="%s"/><text x="%d" y="%d">%s</text>' % (lx + 6, ly - 5, COL[cat], lx + 18, ly, text))
    y += 2 * 24 + 16
    notes = [
        '<tspan font-weight="700">Del bus del MSX llegan a pads:</tspan> A0 a A15 (U1; A1 a A7 también en U4 y U5; A0 también en U3), D0 a D7 (U1 y U2), /RD (U1, U2, U3), /IORQ y /M1 (U4), /SLTSL (U1), +5 V y GND.',
        '<tspan font-weight="700">No llegan a ningún pad</tspan> (hay que cablearlas desde el conector; entre paréntesis, el contacto): ' + ", ".join("%s (%d)" % (STD[n], n) for n in unrouted[:7]) + ",",
        ", ".join("%s (%d)" % (STD[n], n) for n in unrouted[7:]) + ".",
        '<tspan font-weight="700">/IOSEL</tspan> es la salida Y0 de U5: baja en los accesos a los puertos 0x00 y 0x01. En la rev1 sin la corrección 2 está unida a /WAIT (contacto 7).',
        '<tspan font-weight="700">H1</tspan> sale directo del ATmega: SCK, MISO, MOSI y CS son el SPI; SDA y SCL, el I2C; PB0 y PB1, dos pines libres.',
        "Los agujeros sin rotular son el área de islas perforadas. Generado desde sdf1.net (señales), FlyingProbeTesting.json (pads) y los Gerbers (contorno, serigrafía y agujeros).",
    ]
    for i, t in enumerate(notes):
        L.append('<text x="%d" y="%d">%s</text>' % (OX, y + i * 24, t))
    L.append("</g>")
    TH = y + len(notes) * 24 + 10
    TW = W + 2 * OX
    doc = ('<svg xmlns="http://www.w3.org/2000/svg" width="100%%" viewBox="0 0 %.0f %.0f" style="max-width:%.0fpx" font-family="system-ui,sans-serif">' % (TW, TH, TW)
           + '<rect width="100%" height="100%" fill="#fff"/>' + "\n".join(svg) + "\n".join(L) + "</svg>")
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(doc)
    print("mapa_senales: %s" % sys.argv[2])
    print("              %d islas; no llegan a ningun pad: %s" % (free_holes, " ".join(STD[n] for n in unrouted)))


if __name__ == "__main__":
    main()
