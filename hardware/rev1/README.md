# SDF-1 rev1 (v1.1)

Primera tanda fabricada. **Requiere correcciones a mano**: ver [BODGES.md](BODGES.md)
antes de armar o de enchufar la placa.

| Archivo | Qué es |
|---|---|
| `sdf1.epro2` | Fuente EasyEDA Pro. La verdad del diseño; todo lo demás sale de acá |
| `sdf1-gerbers.zip` | Para pedir el PCB. Subir el ZIP tal cual, sin descomprimir |
| `sdf1-sch.pdf` | Esquemático, para leer y depurar sin instalar el CAD |
| `sdf1-bom.csv` | Lista de componentes |
| `sdf1.net` | Netlist Protel. Conectividad en formato abierto, legible sin el CAD |
| `BODGES.md` | Las correcciones a mano de esta revisión |

## Cómo pedir el PCB

El ZIP de Gerbers lleva las capas y los tres drills. Tres cosas **no** salen de
ahí y hay que pedirlas aparte:

- **Espesor 1,6 mm.** Es lo que espera la ranura de cartucho del MSX. Suele ser
  el default, pero conviene verificarlo.
- **Contactos del borde en oro (*gold fingers*), o al menos ENIG.** El cartucho
  se enchufa y desenchufa muchas veces y el HASL se gasta.
- **Bisel de 45° en el borde de contactos.** Opcional y cuesta un poco más, pero
  entra suave y no raspa la ranura.

Antes de subirlo, verificá los Gerbers con un visor independiente del CAD que
los generó — si EasyEDA exporta mal, su propio visor muestra lo mismo.
[gerbv](https://gerbv.github.io/) es libre. Mirá sobre todo que los contactos
del borde estén completos y que el contorno sea una curva cerrada.

## Qué pedir de los pasivos SMD

El BOM no trae código de fabricante para los 0603, así que estas son las
especificaciones que faltan:

| Ref | Cant. | Valor | Especificación |
|---|---|---|---|
| C1, C2 | 2 | 22 pF | 0603 · **C0G / NP0** · 50 V · ±5 % |
| C3–C8 | 6 | 100 nF | 0603 · **X7R** · 50 V · ±10 % |
| R1 | 1 | 10 K | 0603 · ±1 % · 1/10 W |

**"0603" acá es imperial: 1,6 × 0,8 mm** (métrico `1608`). El 0603 métrico es
0,6 × 0,3 mm, o sea un 0201 imperial — otra cosa completamente distinta. Si el
proveedor trabaja en métrico, pedí `1608`.

**C1 y C2 tienen que ser C0G/NP0.** Son las capacidades de carga del cristal:
un X7R deriva con temperatura y con tensión, y eso corre la frecuencia del
oscilador o directamente impide que arranque. En C3–C8, que son de desacople,
X7R está bien. Evitá Y5V en ambos casos: pierden más de la mitad de su
capacidad bajo tensión de trabajo.

Las resistencias que agregan los bodges (2× 4K7 y 1× 10K) conviene que sean
**THT**, no 0603: van soldadas al aire entre patas de integrados.

## Qué hace falta saber soldar

La placa es mixta: los integrados y los conectores son THT, pero los pasivos
(`C1`–`C8`, `R1`) son **0603 SMD**. Son soldables a mano con punta fina y algo
de práctica, pero conviene saberlo antes de encargar.

Montá el ATmega en **zócalo torneado** hasta que exista el bootloader: hoy
reprogramarlo sin sacarlo requiere el cable del paso 5 de BODGES.md.
