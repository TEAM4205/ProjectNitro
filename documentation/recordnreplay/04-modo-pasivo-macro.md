# 04 - Modo pasivo (`.macro`)

Graba **acciones**, no datos. Sirve para macros y autónomos: en lugar de copiar cómo se movían los motores,
se anota "qué se pidió y cuándo" y, al reproducir, se vuelve a pedir lo mismo.

## La idea

```
Modo activo (.match):   "a los 2.30 s el motor estaba a 187 rpm, a los 2.31 s a 190 rpm..."
Modo pasivo (.macro):   "a los 2.30 s: ejecuta moverAutonomo(40, 0)"
```

Un `.macro` es mucho más pequeño (unas decenas de bytes por acción) y robusto: si la acción mueve el robot con un
PID, la repetición se adapta a la batería y al piso, porque vuelve a ejecutar el PID en lugar de copiar voltajes.

## Cómo se conecta con las Actions

Las acciones (`Action`, ver [iron.hpp](../../include/nitrogen/heliosmgr/iron.hpp)) se lanzan con
`ActionManager::trigger(nombre, fase, args)`. Cuando se está grabando una macro, `trigger` avisa al Recorder.

```
 el driver pulsa un botón
          │
          ▼
 ActionManager::trigger(nombre, fase, args)
          │
          ├── ¿la acción tiene intercepciones RECORD_*?
          │        SÍ ──► corre la intercepción de esta fase (si existe)
          │               y el Recorder NO anota nada por su cuenta.
          │               La intercepción decide qué se graba (register_action)
          │        NO ──► el Recorder anota la acción tal cual
          │
          ▼
 (la acción se ejecuta normalmente)
```

> **Importante:** solo se graba lo que pasa por `trigger` (lo que pidió el driver). Las acciones que otras
> acciones lanzan por dentro con `execute` **no** se graban; si se grabaran, al reproducir se ejecutarían dos veces.

## Las intercepciones `extends RECORD_*`

Una acción puede declarar código que se ejecuta **solo mientras se graba**, uno por fase:

| Intercepción | Cuándo corre |
|---|---|
| `RECORD_DOWN` | Al empezar a pulsar el botón |
| `RECORD_PRESSING` | Mientras se mantiene |
| `RECORD_UP` | Al soltarlo |

Ejemplo de [`src/actions.cpp`](../../src/actions.cpp): `tankMove` mueve los motores (algo que no tiene sentido
repetir voltaje por voltaje), así que en vez de eso se graba **cuánto se desplazó**:

```cpp
Action<pressing> tankMove(left, right){ ... motores ... }
extends RECORD_DOWN { /* anota dónde empiezo */ }
extends RECORD_UP   { /* calcula cuánto me moví y graba: */
    Recorder::register_action("moverAutonomo", "", {distanceX, distanceY});
}
```

Resultado en el archivo: un solo evento `moverAutonomo(dx, dy)`, en vez de cientos de valores de motor. Al
reproducir, `moverAutonomo` se ejecuta con su PID.

### `register_action`

```cpp
Recorder::register_action(accion, estado, args, wait_prev = true);
```

- `accion`: nombre de la acción a ejecutar al reproducir.
- `estado`: estado de un toggle (`"ON"`, `"SCORE"`...) o `""` si no aplica.
- `args`: lista de números (`{dx, dy}`), máximo 64.
- `wait_prev`: si es `true` (por defecto), al reproducir este evento **espera a que termine la acción interceptada
  anterior** antes de empezar. Es lo que encadena movimientos PID uno tras otro.

Solo funciona mientras se graba una macro; fuera de eso no hace nada.

## Qué se anota de las acciones sin intercepciones

| Situación | Qué se graba |
|---|---|
| Acción de un solo disparo (`tap down`, `tap up`), toggle o loop | Un evento en el momento del disparo |
| `pressing` (se llama mientras el botón está pulsado) | Un evento **solo cuando cambian sus argumentos** (no 100 por segundo), y otro al soltar |

Un `pressing` mantenido con los mismos argumentos genera un solo evento. Esto mantiene el archivo pequeño.

Una acción solo se anota cuando la fase del botón es la que la dispara de verdad (por ejemplo, una acción
`tap down` se anota al pulsar, no al soltar).

## Paso a paso: qué pasa al grabar

1. **`start_macro("auton_izq")`**: comprueba SD y módulo; vacía la lista de eventos y marca el ActionManager
   como "grabando".
2. Cada `trigger` o `register_action` añade un **evento** a una lista en RAM, protegida por un `Mutex`
   (dos tasks pueden anotar a la vez). Cada evento lleva su tiempo, medido desde `start_macro`.
3. Los nombres de acción y de estado se guardan **una sola vez** en una tabla de textos, y los eventos usan su
   número. Así "moverAutonomo" no se repite cien veces.
4. **`stop()`**: apaga el modo grabación y despierta al *writer*, que escribe cabecera, tabla de textos y eventos
   (en trozos de 4 KB cediendo CPU entre ellos) y deja el archivo cerrado.

Mientras se graba, una macro vive **solo en RAM**; se guarda al parar. Si se apaga el robot antes de `stop()`,
se pierde.

## Reproducir una macro

```cpp
Replayer::load("/usd/auton_izq_00001.macro");   // opcional: la deja lista en RAM
Replayer::play("/usd/auton_izq_00001.macro");
Replayer::wait();
```

### Modo híbrido de tiempos

Cada evento tiene el tiempo en que se grabó. El Replayer lo respeta, con una excepción importante:

| Tipo de evento | Cuándo sale |
|---|---|
| Normal | En su tiempo grabado |
| Con `WAIT_PREV` (los de `register_action`) | En su tiempo grabado, **pero si la acción interceptada anterior todavía no terminó, espera**; el reloj se detiene ahí y sigue cuando termina |

Así los tiempos se respetan cuando se puede, y los movimientos PID se encadenan bien aunque esta vez tarden
más o menos que cuando se grabó.

Ejemplo:

| Evento | Tiempo grabado | Qué pasa al reproducir |
|---|---|---|
| `moverAutonomo(40, 0)` | 0.0 s | Sale a los 0.0 s |
| `moverAutonomo(0, 90)` | 1.5 s | A los 1.5 s mira si el anterior terminó. Si aún se mueve (esta vez tardó 2 s), espera 0.5 s y **entonces** sale. |
| `intake.ON` (sin WAIT_PREV) | 1.6 s | Sale en su tiempo grabado, relativo al reloj ya corrido por la espera |

- Hay un tope de espera de `wait_timeout_ms` (15 s). Si se cumple, avisa por `on_error` y sigue de todos modos,
  para que un movimiento atascado no congele toda la macro.
- `Replayer::config().speed` escala todo el tiempo (0.5 = mitad de velocidad).
- Los eventos `pressing` activos se vuelven a ejecutar cada `period_ms` mientras dura su mantenimiento.
- Reproducir llama a `ActionManager::execute`, no a `trigger`, así que **no** se vuelve a grabar nada.

### Precargar con `load`

`play()` en una macro lee el archivo de la SD. Para un autónomo, donde el tiempo de arranque importa, llama antes
(en `initialize()` o al empezar el autónomo) a `Replayer::load(ruta)`. La macro queda en RAM y `play()` la usa
directamente, sin tocar la SD.

## Cuándo usar activo y cuándo pasivo

| | Activo (`.match`) | Pasivo (`.macro`) |
|---|---|---|
| Qué guarda | Valores de sensores y motores en el tiempo | Acciones y sus argumentos |
| Tamaño | Cientos de KB | Unos pocos KB |
| Fidelidad | Copia el movimiento "tal cual" | Repite la orden; el resultado se adapta |
| Se adapta a batería/piso | No (lazo abierto) | Sí, si las acciones usan PID |
| Ideal para | Analizar partidos, repetir un recorrido | Autónomos, macros |
