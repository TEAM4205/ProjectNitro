# 06 - Formato del archivo `.match`

Descripción byte por byte. Definido en [format.hpp](../../include/nitrogen/recordnreplay/format.hpp) y
[codec.hpp](../../include/nitrogen/recordnreplay/codec.hpp). Los ejemplos con bytes salen de
[`sample.match`](../../3rd_party/match_viewer/sample.match).

**Todos los números van en little-endian** (primero el byte menos significativo), que es el orden nativo
del robot y de los PCs.

## Estructura general

```
┌────────────────────┐
│ Cabecera  (64 B)   │  fija
├────────────────────┤
│ Tabla de canales   │  tamaño = tableBytes (está en la cabecera)
├────────────────────┤
│ Bloque 0           │  cabecera de bloque (24 B) + payload
│ Bloque 1           │
│ ...                │  hasta el final del archivo
└────────────────────┘
```

## 1. Cabecera (64 bytes)

| Offset | Tamaño | Campo | Significado |
|---:|---:|---|---|
| 0 | 4 | `magic` | Texto `NRNR`: identifica el formato |
| 4 | 2 | `version` | Versión del formato (ahora 1) |
| 6 | 1 | `kind` | 1 = `.match`, 2 = `.macro` |
| 7 | 1 | `flags` | bit 0 = **CLEAN**: el archivo se cerró correctamente |
| 8 | 4 | `runId` | Número de la grabación |
| 12 | 4 | `periodUs` | Periodo de muestreo **pedido**, en µs (10000 = 100 Hz). Solo informativo |
| 16 | 4 | `blockBytes` | Tamaño máximo de un bloque |
| 20 | 4 | `tableBytes` | Tamaño de la tabla de canales |
| 24 | 4 | `tableCount` | Cantidad de canales |
| 28 | 4 | `tableCrc` | CRC32 de la tabla |
| 32 | 4 | `frames` | Total de frames. **Válido solo si CLEAN** |
| 36 | 4 | `durationUs` | Duración total en µs. **Válido solo si CLEAN** |
| 40 | 20 | reservado | Ceros |
| 60 | 4 | `crc` | CRC32 de los 60 bytes anteriores |

Ejemplo real (los 16 primeros bytes): `4E 52 4E 52 | 01 00 | 01 | 01 | 01 00 00 00 | 10 27 00 00`
→ magic `NRNR`, versión 1, `.match`, CLEAN, run 1, periodo `0x2710` = 10000 µs.

### Se escribe dos veces

- Al **empezar** a grabar se escribe con CLEAN apagado y `frames` y `durationUs` en 0.
- Al **terminar bien** se vuelve al inicio del archivo y se reescribe con CLEAN, el total real de frames y la
  duración.

Si se corta la energía antes, la cabecera queda sin CLEAN. Un lector no debe fiarse de `frames` ni `durationUs` en
ese caso: debe recorrer los bloques y contar.

## 2. Tabla de canales

Una entrada por canal, en el mismo orden en que aparecen luego los valores de cada frame:

```
string objeto │ string perfil │ string canal │ int8 exp10 │ uint8 every │ uint8 flags
```

Un *string* es **1 byte de largo** seguido de los caracteres (máximo 255, sin terminador).

Ejemplo real, el primer canal (`master / ALL / lx`):

```
06 6D 61 73 74 65 72   03 41 4C 4C   02 6C 78   00   01   01
 6  m  a  s  t  e  r    3  A  L  L    2  l  x   exp10=0  every=1  flags=1
```

| Campo | Significado |
|---|---|
| `exp10` | Valor guardado = valor real × 10^exp10 (punto fijo). Puede ser negativo |
| `every` | Cada cuántos frames se muestrea (1 = todos). Si es 0 se trata como 1 |
| `flags` | 1 = APPLYABLE, 2 = ANGLE, 4 = DIGITAL |

La tabla está protegida por `tableCrc`. Si no coincide, el archivo no se puede interpretar.

## 3. Bloques

Después de la tabla vienen los bloques, uno tras otro, hasta el final del archivo.

### Cabecera de bloque (24 bytes)

| Offset | Tamaño | Campo | Significado |
|---:|---:|---|---|
| 0 | 4 | `magic` | Texto `BLK1` |
| 4 | 4 | `seq` | Número de bloque, desde 0 |
| 8 | 4 | `frame0` | Índice global del primer frame (se usa para la decimación `every`) |
| 12 | 4 | `t0Us` | Tiempo del primer frame, en µs desde el inicio de la grabación |
| 16 | 2 | `nFrames` | Cantidad de frames del bloque |
| 18 | 2 | `payloadLen` | Bytes de datos que siguen |
| 20 | 4 | `crc` | CRC32 **del payload** |

Ejemplo real (bloque 0 de `sample.match`):
`42 4C 4B 31 | 00 00 00 00 | 00 00 00 00 | 00 00 00 00 | 9B 00 | C0 03 | AF 9B 6A 36`
→ `BLK1`, bloque 0, `frame0`=0, `t0Us`=0, 155 frames (`0x9B`), payload de 960 bytes (`0x03C0`).

### Cada bloque se decodifica solo

El primer frame de cada bloque es un **keyframe**: guarda los valores completos, no diferencias. Así:

- Puedes saltar a cualquier bloque sin leer los anteriores.
- Si un bloque se daña (CRC malo), se descarta **solo ese** y el siguiente se lee normal.
- Si el archivo se corta a medias (corte de energía), el único bloque que no se puede leer es el último, el incompleto.

## 4. Frames dentro del payload

Los tipos de datos usados (varint, zigzag, delta) están explicados en [08 - Compresión](08-compresion.md).

**Keyframe (primer frame del bloque):**

```
varint dt │ varint zigzag(valor) × cada canal
```

`dt` vale 0 (el tiempo del primer frame ya está en `t0Us`).

**Frames siguientes:**

```
varint dt │ máscara de bits │ varint zigzag(delta) × cada bit encendido
```

- `dt`: microsegundos desde el frame anterior. Es el tiempo **real**, no el nominal.
- La **máscara** tiene un bit por cada canal que "toca" en este frame (los que cumplen `k % every == 0`, donde
  `k = frame0 + posición del frame en el bloque`). Ocupa `ceil(canales_que_tocan / 8)` bytes. El primer canal que
  toca es el bit 0 del primer byte. Bit en 1 = ese canal cambió; en 0 = sigue igual.
- Los canales que no tocan en este frame conservan su valor anterior y no ocupan ni un bit.
- Cada delta es la diferencia con el valor anterior del canal, en aritmética de 32 bits con desborde.

### Ejemplo real

Primeros bytes del payload de `sample.match` (7 canales: lx, ly, rx, ry, buttons, pos, temp):

```
8F 4B | 04 | FE 01 | 02 | 00 | 00 | 56 | 3C | A4 4D ...
```

| Bytes | Significado |
|---|---|
| `8F 4B` | `dt` = 15 + (0x4B << 7) = **9615 µs** |
| `04` | lx = zigzag⁻¹(4) = **2** |
| `FE 01` | ly = zigzag⁻¹(254) = **127** |
| `02` | rx = **1** |
| `00` | ry = **0** |
| `00` | buttons = **0** |
| `56` | pos = zigzag⁻¹(86) = **43** |
| `3C` | temp = zigzag⁻¹(60) = **30** |
| `A4 4D` | empieza el siguiente frame: `dt` = 36 + (0x4D << 7) = 9892 µs |

## 5. Cómo leer un `.match` (algoritmo)

```
1. Leer 64 bytes. Comprobar magic, version, kind == 1 y crc (de los primeros 60 bytes).
2. Leer tableBytes. Comprobar tableCrc. Decodificar las tableCount entradas.
3. Repetir mientras queden al menos 24 bytes:
     a. Leer la cabecera de bloque. Si magic != "BLK1": parar (no se sabe dónde sigue).
     b. Si faltan bytes para el payload: parar (archivo cortado).
     c. Si crc32(payload) != crc: saltar este bloque (pos += payloadLen) y seguir.
     d. Decodificar nFrames frames:
          t = t0Us
          primer frame: leer dt (suma a t) y los n valores absolutos
          resto: leer dt, calcular qué canales tocan, leer la máscara,
                 por cada bit en 1 leer un delta y sumarlo al valor del canal
```
Una implementación de referencia en Python está en
[`parse_match`](../../3rd_party/match_viewer/match_viewer.py) y una en C++ en
[`MatchDecoder`](../../include/nitrogen/recordnreplay/codec.hpp).

## 6. Integridad y recuperación

| Qué falla | Qué pasa |
|---|---|
| Cabecera o tabla dañadas | El archivo se rechaza (CRC) |
| Un bloque dañado | Ese bloque se salta; los demás se leen. Queda un hueco en el tiempo |
| Archivo cortado a mitad de un bloque | El último bloque incompleto se descarta |
| Energía cortada antes de `stop()` | Cabecera sin CLEAN. Se pierde lo que aún no se había volcado a la SD (el Recorder hace `fflush` cada segundo, así que normalmente es menos de 1 s, más lo que esté esperando en el anillo si la SD iba atrasada) |
| Cabecera de bloque con `magic` roto | Se deja de leer ahí (no hay forma de saber el largo) |

## 7. Límites

- Hasta **255 canales** por grabación.
- Hasta **65 535 frames** y **65 535 bytes de payload** por bloque (por eso `nFrames` y `payloadLen` son de 16 bits).
- Tiempo de 32 bits en µs: unos **71 minutos** como máximo.
- No hay índice al final del archivo. Para buscar un instante hay que recorrer las cabeceras de bloque (cada una
  trae `t0Us` y `payloadLen`, así que no hace falta decodificar los frames).

## 8. Tamaño típico

| Contenido | Bytes por frame (medidos) |
|---|---|
| 6 canales con valores que cambian casi siempre (prueba aleatoria) | 5.7 |
| 7 canales de `sample.match` (sticks moviéndose + motor) | 6.7 |
| Sin compresión, para comparar (6 canales de 4 B + tiempo) | 28 |

Con el control solo (5 canales), a 100 Hz son unos 600 bytes por segundo, unos 180 KB en 5 minutos. Un motor
quieto cuesta 1 bit por frame.
