# Hardware

Una carpeta por revisión de PCB. Quien tenga una placa en la mano necesita los
documentos **de esa** revisión, aunque exista una posterior.

| Revisión | Estado |
|---|---|
| [rev1](rev1/) | Fabricada. Requiere correcciones a mano — ver [rev1/CORRECCIONES.md](rev1/CORRECCIONES.md) |

## Regla para los archivos derivados

`sdf1.epro2` es la única fuente. El PDF, el BOM, el netlist y los Gerbers se
**regeneran todos juntos** cada vez que se toca la fuente, y se commitean en el
mismo commit.

Un derivado desactualizado es peor que no tenerlo: el que lo lea le va a creer.

Los mapas SVG de cada revisión (`cortes-rev1.svg`, `senales-rev1.svg`) también
son derivados: salen de los Gerbers y del netlist con `make mapas`, y se
regeneran en el mismo commit que ellos.

Los nombres no llevan fecha ni versión a propósito — la revisión la da la
carpeta y la historia la lleva git. Así un cambio se ve como un diff y no como
un archivo nuevo.
