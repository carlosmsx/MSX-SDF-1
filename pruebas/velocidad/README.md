# Prueba de velocidad del disco

`SDFBENCH.BAS` mide la velocidad del disco desde Disk BASIC, para comparar
versiones del firmware y de la ROM sobre la misma imagen.

## Qué mide

| Prueba | Cómo | Resultado |
|---|---|---|
| Escritura | 8 veces `BSAVE` de 16 KB | KB/s |
| Lectura | 8 veces `BLOAD` del mismo archivo | KB/s |
| Verificación | Compara 529 bytes repartidos en los 16 KB leídos con la BIOS, que es de donde salieron | Errores; tiene que dar 0 |
| Acceso al azar | 64 `GET` de 128 bytes en sectores distintos | ms por `GET` |

Las dos primeras pruebas miden transferencias largas. El acceso al azar hace
una llamada a DSKIO por cada `GET`, así que mide sobre todo el costo fijo de
cada llamada.

Los datos son una copia de la BIOS (`0000h`-`3FFFh`) en `9800h`-`D7FFh`: cada
sector es distinto, y un sector cambiado por otro se nota en la verificación.
Antes de leer, esa zona se llena de ceros.

## Uso

1. Copiar `SDFBENCH.BAS` a una imagen `.DSK` con al menos 17 KB libres.
2. En el MSX, con esa imagen montada en la unidad actual:

   ```
   RUN "SDFBENCH.BAS"
   ```

3. Escribir una etiqueta que identifique la corrida (por ejemplo `v0.2` o
   `cef1cf5`).

Cada corrida se agrega a `RESULT.TXT` en la misma imagen, una línea por
corrida:

```
etiqueta, KB/s escritura, KB/s lectura, ms por GET, errores
```

Para verlo, `TYPE RESULT.TXT` desde MSX-DOS.

## Notas

- El tiempo sale de `TIME`, que cuenta interrupciones de video. La frecuencia
  (50 o 60 Hz) se lee del byte de identificación de la BIOS en `002Bh`.
- La resolución es de 1/50 o 1/60 s. Con los tiempos actuales cada prueba dura
  varios segundos; si se vuelve muy rápida, subir `N` (línea 80).
- El programa usa `CLEAR 300,&H9800` y necesita que `HIMEM` esté por encima
  de `D820h`. Si no, lo avisa y termina.
- El archivo de prueba `BENCH.BIN` se borra al terminar.
