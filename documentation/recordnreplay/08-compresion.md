# 08 - Compresión

Cómo se guardan frames de decenas de canales en unos pocos bytes, **sin perder ni un dato**. Es una compresión
sin pérdida: lo que lees es exactamente lo que grabaste. Código en
[codec.hpp](../../include/nitrogen/recordnreplay/codec.hpp).

## Las cuatro ideas

1. **Delta**: en vez del valor, guardar cuánto cambió respecto al anterior.
2. **Zigzag**: transformar números con signo para que los pequeños (positivos o negativos) ocupen poco.
3. **Varint**: usar 1 byte para números pequeños y más solo cuando hace falta.
4. **Máscara de cambios**: un bit por canal que dice si cambió; los que no cambiaron no ocupan nada.

## 1. Delta

Un motor a velocidad casi constante da valores así: `1875, 1876, 1875, 1877, 1875`.
En vez de guardar cada uno (4 bytes cada uno), se guarda el primero y luego solo las diferencias:
`1875, +1, -1, +2, -2`. Las diferencias son números pequeños.

Los deltas se calculan en aritmética de **32 bits sin signo**, que da la vuelta al desbordar. Eso hace que incluso
un salto enorme (de -2 147 483 648 a +2 147 483 647 y vuelta) se recupere **exactamente**.

## 2. Zigzag

Los números negativos en complemento a dos (`-1` = `0xFFFFFFFF`) son enormes si se leen como sin signo. El zigzag los
reordena para que los de valor absoluto pequeño queden pequeños:

```
zigzag(n) = (n << 1) XOR (n >> 31)        // n >> 31 es 0 o -1 según el signo
```

| n | -3 | -2 | -1 | 0 | 1 | 2 | 3 |
|---|---:|---:|---:|---:|---:|---:|---:|
| zigzag(n) | 5 | 3 | 1 | 0 | 2 | 4 | 6 |

Para deshacerlo: `n = (u >> 1) XOR -(u & 1)`.

## 3. Varint

Un entero sin signo se parte en trozos de **7 bits**. Cada byte usa su bit más alto como "sigue otro byte".
El primer byte lleva los 7 bits menos significativos.

| Valor | Bytes | Ejemplo |
|---|---:|---|
| 0 a 127 | 1 | 127 → `7F` |
| 128 a 16 383 | 2 | 300 → `AC 02` |
| 16 384 a 2 097 151 | 3 | |
| hasta 268 435 455 | 4 | |
| hasta 4 294 967 295 | 5 | |

Ejemplo, 300 = `100101100` en binario:
- 7 bits bajos: `0101100` (0x2C), hay más → byte `0xAC` (0x2C + bit alto)
- Siguientes bits: `10` (2), último → byte `0x02`
- Resultado: `AC 02`.

Al decodificar, se lee byte a byte sumando `(byte & 0x7F) << (7 × posición)` hasta encontrar un byte sin el bit alto.
Un varint de más de 5 bytes es inválido y se rechaza.

## 4. Máscara de cambios

En cada frame (menos en el keyframe), antes de los valores va una **máscara**: un bit por canal que "toca" en este
frame. Bit encendido = el canal cambió, y a continuación hay un delta para él.

Ejemplo con 5 canales, solo cambian el 1.º y el 4.º:

```
máscara:  1 0 0 1 0   →  un byte: 0b00001001 = 0x09
deltas:   zigzag(delta_canal1)  zigzag(delta_canal4)    // solo estos dos
```

Un robot parado, con 5 canales sin cambios, ocupa por frame: `dt` (1 o 2 bytes) + 1 byte de máscara = **2-3 bytes**.

## Decimación: canales que no tocan

Un canal con `every = N` solo "toca" en los frames cuyo índice `k` cumple `k % N == 0`, donde
`k = frame0 + posición del frame en el bloque`. En los demás frames:

- No tiene bit en la máscara.
- Conserva su último valor.

Como la máscara solo cuenta los canales que tocan, tanto el codificador como el decodificador saben qué bits hay
sin necesidad de guardarlos.

## El frame completo

```
keyframe (1.º del bloque):   varint dt │ [varint zigzag(valor)] × todos los canales
resto:                       varint dt │ máscara │ [varint zigzag(delta)] × canales con bit encendido
```

`dt` es el tiempo en microsegundos desde el frame anterior. Un `dt` de 10 000 µs ocupa 2 bytes.

### Ejemplo trabajado

Dos frames de 3 canales (`a`, `b`, `c`; `every = 1`) con tiempos 10 000 y 20 000 µs, el bloque empieza en `t0 = 10000`:

| | a | b | c |
|---|---:|---:|---:|
| frame 0 | 100 | -5 | 0 |
| frame 1 | 100 | -3 | 0 |

- **Frame 0 (keyframe):** `dt = 0` → `00`; luego `zigzag(100)=200` → `C8 01`; `zigzag(-5)=9` → `09`; `zigzag(0)=0` → `00`.
  Bytes: `00 C8 01 09 00` (5 bytes).
- **Frame 1:** `dt = 10000` → `90 4E`; solo cambió `b`: máscara `010` = `0x02`; delta `+2` → `zigzag = 4` → `04`.
  Bytes: `90 4E 02 04` (4 bytes).

Total: 9 bytes, frente a 2 frames × (3×4 + 4) = 32 sin comprimir.

## Peor caso y cierre de bloque

Un frame puede ocupar como máximo:

```
5 (dt) + ceil(canales / 8) (máscara) + 5 × canales (un varint de 5 bytes por canal)
```

El codificador solo agrega un frame al bloque si ese **máximo** cabe en lo que queda. Por eso un bloque se
cierra un poco antes de estar totalmente lleno; a cambio nunca hay que "deshacer" un frame que no cupo.

Por ese mismo cálculo, el bloque debe ser al menos tan grande como el peor frame. `Recorder::start_match` lo
asegura agrandando el tamaño de bloque si hace falta (hasta 65 535 bytes).

## Eventos de macro

Los eventos de `.macro` usan varint para tiempos e índices, y `float32` para argumentos. No llevan delta ni máscara.
Ver [07](07-formato-macro.md).

## Integridad: CRC32

Cada bloque lleva el **CRC32** de su payload (el estándar de zlib, polinomio `0xEDB88320`). Al leer se recalcula;
si no coincide, el bloque se descarta. La cabecera y la tabla también tienen el suyo.

El CRC se calcula con una **tabla de 256 entradas**, un byte por vez. Se hace una sola vez por bloque (~4 KB), no
por frame.

## Coste en CPU y por qué no hay ensamblador

Por cada frame, el trabajo es unas pocas instrucciones enteras por canal (desplazar, comparar, escribir un byte).
Lo que realmente cuesta en cada frame son las **lecturas de sensores en `sample()`**, no la compresión.

No se usa ensamblador ARM ni NEON porque:

- El Cortex-A9 del Brain **no tiene** instrucción de CRC (llegó en ARMv8), así que no hay un atajo en hardware.
- Con unas pocas decenas de canales, procesarlos por NEON no compensa el costo de prepararlo.
- El código en C++ ya se compila con `-Os` y es lo bastante rápido; la prioridad del módulo es que no se
  pierdan muestras, y eso lo asegura la separación entre medir y escribir (ver [02](02-arquitectura.md)).

## Cuánto se comprime

Medidas del test (`tests/rnr_codec_test.cpp`) y del archivo de ejemplo:

| Caso | Bytes por frame | Sin comprimir |
|---|---:|---:|
| 6 canales, valores que cambian casi siempre | 5.7 | 28 |
| 7 canales (sticks moviéndose + motor) | 6.7 | 32 |
| Robot quieto, 5 canales | 3 | 24 |

Cuanto menos cambien los datos, más pequeño queda el archivo.
