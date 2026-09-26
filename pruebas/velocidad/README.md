# Prueba de velocidad del disco

`SDFBENCH.BAS` mide la velocidad del disco desde Disk BASIC, para comparar
versiones del firmware y de la ROM sobre la misma imagen.

## Qué mide

| Prueba | Cómo | Resultado |
|---|---|---|
| Escritura | 32 veces `BSAVE` de 16 KB (512 KB) | KB/s |
| Lectura | 32 veces `BLOAD` del mismo archivo | KB/s |
| Verificación | Compara 529 bytes repartidos en los 16 KB leídos con la BIOS, que es de donde salieron | Errores; tiene que dar 0 |
| Acceso al azar | 256 `GET` de 128 bytes en sectores distintos | ms por `GET` |

Las dos primeras pruebas miden transferencias largas. El acceso al azar hace
una llamada a DSKIO por cada `GET`, así que mide sobre todo el costo fijo de
cada llamada.

Los datos son una copia de la BIOS (`0000h`-`3FFFh`) en `9800h`-`D7FFh`: cada
sector es distinto, y un sector cambiado por otro se nota en la verificación.
Antes de leer, esa zona se llena de ceros.

## Cómo mide el tiempo

**Con el reloj `RTC:`, no con `TIME`.** `TIME` cuenta interrupciones de video,
y durante el acceso al disco las interrupciones están cortadas: la primera
versión de esta prueba, que usaba `TIME`, dio 156 KB/s de lectura, más de tres
veces el máximo posible con la ROM actual (unos 45 KB/s).

El reloj tiene resolución de un segundo, así que cada prueba:

1. Espera a que cambie el segundo y arranca justo ahí.
2. Al terminar, espera el cambio de segundo siguiente, y mide con `TIME` cuánto
   esperó. Esa espera no toca el disco, así que ahí `TIME` sí sirve.
3. El tiempo es la diferencia de segundos menos esa espera.

El firmware relee el DS1307 cada 250 ms, así que cada medición tiene un error de
hasta ±0,25 s. Por eso cada prueba dura del orden de 10 a 30 segundos.

Si no hay reloj (el `OPEN` falla o el reloj nunca se puso), la prueba avisa y
usa `TIME`. Esos resultados dan de más y quedan marcados como `TIME` en
`RESULT.TXT`.

## Uso

1. Copiar `SDFBENCH.BAS` a una imagen `.DSK` con al menos 17 KB libres.
2. En el MSX, con esa imagen montada:

   ```
   RUN "SDFBENCH.BAS"
   ```

3. Escribir una etiqueta que identifique la corrida (por ejemplo `v0.2` o
   `cef1cf5`).

Cada corrida se agrega a `RESULT.TXT` en la misma imagen, una línea por
corrida:

```
etiqueta, KB/s escritura, KB/s lectura, ms por GET, errores, reloj
```

Para verlo, `TYPE RESULT.TXT` desde MSX-DOS.

## Notas

- Si una prueba se vuelve demasiado rápida, subir `N` o `M` (línea 90).
- El programa usa `CLEAR 300,&H9800` y necesita que `HIMEM` esté por encima
  de `D820h`. Si no, lo avisa y termina.
- El archivo de prueba `BENCH.BIN` se borra al terminar.
