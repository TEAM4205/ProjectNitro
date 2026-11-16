# 11 - Herramientas y pruebas

## Visor de `.match` en Python

Ubicación: [`3rd_party/match_viewer/match_viewer.py`](../../3rd_party/match_viewer/match_viewer.py).
Solo usa la librería estándar de Python (tkinter); no hay nada que instalar.

```
python 3rd_party/match_viewer/match_viewer.py [archivo.match]
```

Sin argumentos abre un selector de archivos. Copia el `.match` de la SD al PC y ábrelo.

### Qué muestra

- Los dos **sticks** con su posición y valores numéricos (LX/LY, RX/RY).
- Los **12 botones**, iluminados en verde mientras están pulsados.
- Una **barra de tiempo** para saltar a cualquier punto, botón Play/Pausa, velocidad de 0.25x a 4x, y el tiempo
  actual con el número de frame.
- Un selector, si el archivo trae `master` y `partner`.
- Una línea de estado: frames, duración, Hz reales frente a nominales, y avisos si hay bloques corruptos o si el
  archivo no se cerró limpio.

Atajos: **espacio** = play/pausa, **flechas izquierda/derecha** = ±1 segundo.

### Lo que no hace

- Solo muestra el control. Los canales de motores y sensores se decodifican (para no desalinear la lectura) pero no se dibujan.
- No abre `.macro`.
- Muestra el último frame que ya ocurrió, sin interpolar: son los datos crudos, no lo que reproduciría el Replayer.

### Cómo lee el archivo

La función `parse_match` del visor es una implementación de referencia del formato (ver
[06 - Formato `.match`](06-formato-match.md)). Si cambias el formato, hay que actualizar **también** ese
archivo y [codec.hpp](../../include/nitrogen/recordnreplay/codec.hpp).

## Archivo de ejemplo

[`3rd_party/match_viewer/sample.match`](../../3rd_party/match_viewer/sample.match): 10 segundos de un control
simulado (sticks en movimiento y varios botones pulsados) más un motor ficticio. Sirve para probar el visor sin robot.

Se genera con [`tests/make_sample_match.cpp`](../../tests/make_sample_match.cpp), que usa el codec real:

```
g++ -std=c++20 -O2 tests/make_sample_match.cpp -o make_sample
./make_sample sample.match [valores_esperados.csv]
```

El CSV opcional trae exactamente lo que se codificó (tiempo, 4 ejes, botones, posición), por si quieres comparar
con lo que lee el visor.

## Test del formato y la compresión

[`tests/rnr_codec_test.cpp`](../../tests/rnr_codec_test.cpp) corre en el PC (no necesita PROS ni robot):

```
g++ -std=c++20 -Wall -Wextra -O2 tests/rnr_codec_test.cpp -o rnr_test
./rnr_test
```

Prueba:

- Varint y zigzag con valores pequeños, los límites de 1 y 2 bytes (63, 64, 127, 128), 2^20 y los extremos
  `INT32_MIN` / `INT32_MAX`, y con varints inválidos o cortados.
- **Ida y vuelta** completa de un `.match` en varios bloques: 3000 frames, 6 canales con distintas decimaciones,
  saltos extremos, tramos quietos, y tiempos irregulares (9 a 12 ms).
- Un bloque con un byte cambiado (CRC mal), un bloque cortado y una cabecera demasiado corta: todos se rechazan.
- Cabecera y tablas (de canales y de textos): ida y vuelta, y rechazo de datos truncados o con CRC alterado.
- Eventos de macro: ida y vuelta, y rechazo de archivos cortados o con índices fuera de la tabla.
- El anillo: llenarlo, vaciarlo y volver a usarlo.
- Interpolación (HOLD, LINEAR, CUBIC), ángulos que cruzan 0°, canales digitales y el filtro EMA.

Debe terminar con `todo OK`.

### Qué no cubre

El test usa solo C++ puro. **No prueba** las tasks, la SD ni la integración con PROS: el Recorder y el Replayer
(`recorder.hpp`, `replayer.hpp`) solo se pueden probar en el robot.

## Comprobar la compilación para el robot

El código del módulo se puede comprobar con el compilador ARM de PROS sin necesidad de `pros make`:

```
arm-none-eabi-g++ -mcpu=cortex-a9 -mfpu=neon-fp16 -mfloat-abi=hard -Os -mthumb \
  -D_POSIX_THREADS -D_UNIX98_THREAD_MUTEX_ATTRIBUTES -D_POSIX_TIMERS -D_POSIX_MONOTONIC_CLOCK \
  -Wall -Wextra -Wno-psabi -Wno-deprecated-declarations --std=gnu++26 \
  -Iinclude -iquote./include -fsyntax-only src/main.cpp
```

Ese comando verifica la sintaxis, pero no genera el programa ni lo prueba.

## Prueba recomendada en el robot

1. Grabar 15 segundos con el control y un motor, y revisar `Recorder::stats()`:
   `dropped_frames` en 0, `max_write_ms` razonable, y `min_dt_us` / `max_dt_us` cerca de 10 000.
2. Abrir el archivo en el visor y comprobar que lo que ves es lo que hiciste.
3. Reproducirlo y comparar.
4. Grabar una macro con una acción interceptada (por ejemplo `moverAutonomo`) y reproducirla.
5. Con `auto_match` activado, pasar de autónomo a driver y comprobar que el archivo queda cerrado (con CLEAN).
