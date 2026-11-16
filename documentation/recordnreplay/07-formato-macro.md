# 07 - Formato del archivo `.macro`

Definido en [format.hpp](../../include/nitrogen/recordnreplay/format.hpp) y
[codec.hpp](../../include/nitrogen/recordnreplay/codec.hpp). Mismos tipos de datos que el
[`.match`](06-formato-match.md) (little-endian, varint, zigzag).

## Estructura general

```
┌────────────────────────┐
│ Cabecera (64 B)        │  la misma estructura que .match, con kind = 2
├────────────────────────┤
│ Tabla de textos        │  nombres de acciones y de estados
├────────────────────────┤
│ Eventos                │  hasta el final del archivo
└────────────────────────┘
```

A diferencia de un `.match`, **no hay bloques**: una macro es pequeña y se carga **entera en RAM** antes de
reproducir. Eso hace que el arranque de un autónomo sea inmediato.

## 1. Cabecera (64 bytes)

Es la misma estructura que en [06](06-formato-match.md#1-cabecera-64-bytes). Cambia el significado de algunos campos:

| Campo | En una `.macro` |
|---|---|
| `kind` | 2 |
| `flags` | bit 0 = CLEAN (siempre encendido al guardar bien) |
| `periodUs` | El `period_ms` del Recorder en µs. **No se usa** al reproducir |
| `blockBytes` | 0 (no hay bloques) |
| `tableBytes` | Tamaño de la tabla de textos |
| `tableCount` | Cantidad de textos |
| `tableCrc` | CRC32 de la tabla de textos |
| `frames` | **Cantidad de eventos** |
| `durationUs` | Tiempo del último evento |

## 2. Tabla de textos

Una lista de *strings* (1 byte de largo + caracteres). Los eventos no repiten nombres, sino que apuntan a un
número en esta lista.

| Índice | Texto |
|---:|---|
| 0 | `""` (vacío: "sin estado") |
| 1... | Los nombres de acciones y de estados, en el orden en que aparecieron al grabar |

Ejemplo: `["", "moverAutonomo", "elevacion", "SCORE"]` ocupa `00 | 0D "moverAutonomo" | 09 "elevacion" | 05 "SCORE"`.

## 3. Eventos

Cada evento se codifica así:

```
varint dt │ varint accion │ varint estado │ u8 (fase | banderas << 2) │ varint argc │ argc × float32
```

| Campo | Significado |
|---|---|
| `dt` | Microsegundos desde el evento anterior (el primero, desde que empezó la grabación) |
| `accion` | Índice en la tabla de textos |
| `estado` | Índice en la tabla de textos (0 = sin estado) |
| `fase` (bits 0-1) | 0 = DOWN, 1 = PRESSING, 2 = UP |
| `banderas` (bits 2-7) | Bit 0 = **WAIT_PREV**, bit 1 = **INTERCEPTED** (ver abajo) |
| `argc` | Cantidad de argumentos (máximo 64) |
| argumentos | Cada uno es un `float32` de 4 bytes |

### Banderas

| Bandera | Significa |
|---|---|
| `WAIT_PREV` (1) | Al reproducir, espera a que termine la acción interceptada anterior antes de salir |
| `INTERCEPTED` (2) | El evento viene de `Recorder::register_action` (una intercepción `RECORD_*`) |

Un `register_action` normal las lleva ambas (`wait_prev = true` por defecto); con `wait_prev = false`
lleva solo `INTERCEPTED`.

### Ejemplo con bytes

Evento: `moverAutonomo(12.5, -30.25)` a los 0 µs, desde una intercepción, con `WAIT_PREV`.

```
00 | 01 | 00 | 0C | 02 | 00 00 48 41 | 00 00 F2 C1
```

| Bytes | Significado |
|---|---|
| `00` | `dt` = 0 |
| `01` | acción = textos[1] = `moverAutonomo` |
| `00` | estado = textos[0] = `""` |
| `0C` | fase 0 (DOWN) + banderas 3 << 2 = `0x0C` (WAIT_PREV + INTERCEPTED) |
| `02` | 2 argumentos |
| `00 00 48 41` | 12.5 como `float32` |
| `00 00 F2 C1` | -30.25 como `float32` |

Son **13 bytes** para este evento.

## 4. Cómo leer una `.macro`

```
1. Leer 64 bytes y comprobar magic, version, kind == 2 y crc.
2. Leer tableBytes. Comprobar tableCrc. Decodificar tableCount strings.
3. Leer eventos hasta el final: dt, acción, estado, byte de fase/banderas, argc, argumentos.
   - Cada evento suma su dt al tiempo acumulado.
   - Rechazar el archivo si: un índice de acción/estado cae fuera de la tabla, argc > 64,
     o faltan bytes.
```

## 5. Integridad

- **Cabecera y tabla de textos** tienen CRC.
- **Los eventos no tienen CRC propio.** Si el archivo está truncado o dañado en esa zona, el lector lo detecta
  cuando algún evento queda incompleto o apunta fuera de la tabla, y rechaza **toda** la macro
  (`"macro corrupta"`). No hay recuperación parcial como en los `.match`.
- La macro se escribe **al parar**: no hay archivo parcial en disco si se corta la energía a mitad de la grabación.

## 6. Detalles

- **Precisión de argumentos:** `float32` (unas 7 cifras). Suficiente para distancias, ángulos y velocidades; no uses
  argumentos que necesiten más de 7 cifras significativas.
- **Eventos `pressing`:** solo se guarda un evento cuando cambian los argumentos, y otro (UP, sin argumentos) al soltar.
- **Tamaño:** 5 bytes por evento sin argumentos, más 4 por cada argumento. Una macro de 100 acciones con dos
  argumentos cada una ocupa 1.3 KB.
- **Tiempo:** 32 bits en µs (unos 71 minutos).
