# 12 - Problemas y límites

## Estado de verificación

Para ser claros sobre qué está comprobado y qué no:

| Qué | Estado |
|---|---|
| Formato, compresión, CRC, anillo, interpolación | **Probado** en el PC con `tests/rnr_codec_test.cpp` |
| Lectura del `.match` en Python contra lo que escribe el codec de C++ | **Probado** (1000 frames idénticos) |
| `main.cpp` y el código del módulo compilan con el toolchain ARM de PROS | **Probado** (solo sintaxis, `-fsyntax-only`) |
| El ejemplo de `NeoMotor` de [05](05-crear-un-neobject.md) | **Compilado** (solo sintaxis) |
| Grabar y reproducir en el robot real (tasks, SD, tiempos) | **No probado** |
| Latencia real de tu SD, rendimiento en un partido real | **No probado** |

## Mensajes de error

Los errores se imprimen por la terminal, se guardan en `last_error()` y llaman a `on_error` si lo definiste.

### Recorder

| Mensaje | Qué significa | Qué hacer |
|---|---|---|
| `falta rnr::init() en initialize()` | No llamaste a `rnr::init()` | Agrégalo en `initialize()` |
| `ya se esta grabando o guardando` | Hay otra grabación activa o terminando de guardarse | Espera a `is_busy() == false` |
| `no hay tarjeta SD` | El Brain no detecta la SD | Revisa que esté puesta (FAT32) |
| `no hay objetos attacheados` | `start_match` sin ningún `attach` | Haz `attach_controller()` o `attach(obj)` antes |
| `no se puede attachear mientras se graba` | `attach` durante una grabación | Hazlo antes de `start_match` |
| `<objeto> no tiene el perfil <perfil>` | El nombre del perfil no existe | Revisa `profile_name()` del objeto |
| `demasiados canales (maximo 255): usa perfiles mas chicos` | Los objetos attacheados suman más de 255 canales | Usa perfiles con menos canales |
| `los objetos attacheados no tienen canales` | Un `describe` devolvió 0 canales | Revisa el objeto |
| `sin memoria para el buffer de grabacion` | No se pudo reservar el anillo | Baja `ring_bytes` |
| `no se pudo crear <ruta>` | No se pudo abrir el archivo | SD llena, protegida o `directory` inválido |
| `error escribiendo en la SD (llena o retirada?)` | Una escritura falló a mitad | La grabación se detiene; el archivo queda sin CLEAN |
| `error escribiendo <ruta>` | Falló al guardar una macro | Revisa la SD |

### Replayer

| Mensaje | Qué significa | Qué hacer |
|---|---|---|
| `no hay archivo para reproducir` | Le pasaste una ruta vacía | Revisa que `Recorder::last_file()` no esté vacío |
| `ya se esta reproduciendo` | `play` con otra reproducción activa | `stop()` o `wait()` antes |
| `no se pudo leer <ruta> (no existe o no es .match/.macro)` | Archivo inexistente o formato inválido | Revisa la ruta y que no esté corrupto |
| `<ruta> no es un .match valido` / `... .macro valido` | Cabecera inválida | Archivo dañado o de otro formato |
| `<ruta>: tabla de canales corrupta` | La tabla no pasó el CRC | Archivo dañado |
| `<ruta>: macro corrupta` | Eventos truncados o fuera de rango | Archivo dañado: hay que grabarla de nuevo |
| `'<objeto>' no existe: sus canales se ignoran (attach o bind)` | El archivo trae un objeto que este programa no tiene | `Recorder::attach` ese objeto o `Replayer::bind` |
| `'<objeto>' ya no tiene el perfil <perfil> como se grabo: se ignora` | El perfil cambió de canales desde que se grabó | Vuelve a grabar, o restaura el perfil |
| `sin memoria para el replay` | No se pudo reservar el anillo de lectura | Baja `ring_bytes` |
| `timeout esperando a <accion>: sigo igual` | Una acción interceptada tardó más de `wait_timeout_ms` | Revisa que la acción termine; sube el tope |
| `no se puede cargar mientras se reproduce` | `load` durante una reproducción | Llama a `load` antes |

## Problemas frecuentes

### Se pierden frames (`dropped_frames` > 0)
La SD se atrasó más de lo que cubre el anillo. Sube `Recorder::config().ring_bytes`, graba menos canales,
o baja la frecuencia (`period_ms = 20`).

### El archivo no tiene CLEAN / el visor avisa "no se cerró limpio"
Se cortó la energía o hubo un error de SD antes de terminar. Los datos anteriores siguen siendo legibles. Para
evitarlo, siempre llama a `Recorder::stop()` y espera a `is_busy() == false` antes de apagar.

### Al pulsar X reproduce el archivo anterior
Pulsaste X mientras el nuevo todavía se estaba guardando. Comprueba `!Recorder::is_busy()` antes de reproducir.

### La macro no graba mi acción
Una macro solo captura lo que pasa por `ActionManager::trigger`. Si en tu perfil la regla llama a un objeto
directamente (`arm.execute()`), no pasa por `trigger` y no se graba. Y si la acción tiene intercepciones
`RECORD_*`, solo se graba lo que ellas anoten con `register_action`.

### Un `.match` se reproduce con un objeto sin moverse
Mira `Replayer::last_error()`: lo más probable es que el objeto no exista (`bind`/`attach`) o que su perfil haya
cambiado desde la grabación.

### Mi botón de cancelar no responde al reproducir
Mientras reproduces un control, los perfiles ven el **control grabado**, no el real. Lee el botón de cancelar
directamente del `pros::Controller` en `opcontrol`.

### El replay se aleja de lo que hizo el robot
El `.match` es **lazo abierto**: repite las órdenes, no la posición. Cambios de batería, fricción o el punto de
partida desvían el resultado. Parte siempre de la misma posición. Para movimientos precisos usa una `.macro` con
acciones controladas por PID.

## Límites

| Límite | Valor |
|---|---|
| Canales por grabación | 255 |
| Frames por bloque | 65 535 |
| Bytes de payload por bloque | 65 535 |
| Argumentos por evento de macro | 64 |
| Duración (tiempo de 32 bits en µs) | ~71 minutos |
| Precisión de los argumentos de una macro | `float32` (~7 cifras) |
| Resolución del tiempo | 1 ms (reloj de PROS); el periodo es en ms enteros |
| Frecuencia útil | 100 Hz (VEXos publica datos nuevos cada 10 ms) |

## Qué todavía no hace

- **No hay índice** al final del `.match`: buscar un instante implica recorrer las cabeceras de los bloques.
- **No hay objetos concretos** incluidos (motor, IMU, rotación...): solo la interfaz. Los defines tú
  (ver [05](05-crear-un-neobject.md)).
- **Los hooks** (`ON RECORD_START` / `ON RECORD_STOP`) todavía no reciben avisos del Recorder, porque el
  `HookManager` aún no existe.
- **Las macros viven en RAM** hasta `stop()`: si se apaga el robot antes, se pierden.
- **Una macro dañada se rechaza entera**: sus eventos no tienen CRC individual.
- **El visor de Python** no dibuja motores ni sensores, y no abre macros.
- **No se valida `period_ms`**: un valor de 0 o excesivamente bajo da resultados impredecibles. Si lo cambias
  en medio de una grabación, la cabecera puede quedar con un periodo distinto del usado.
- **`Replayer::load`** solo sirve para macros; los `.match` se leen en streaming.

## Un error conocido fuera del módulo

En [`src/actions.cpp`](../../src/actions.cpp), `extends RECORD_DOWN` escribe `int posX = odometria.posX;`. En C++
eso crea una variable **local** nueva, no actualiza el `posX` global. Como resultado, `RECORD_UP` calcula la
distancia con el `posX` global, que nunca se actualiza (se queda en 0). Hay que asignar sin declarar de nuevo (`posX = odometria.posX;`). Es parte
de la sintaxis neo, no del módulo.
