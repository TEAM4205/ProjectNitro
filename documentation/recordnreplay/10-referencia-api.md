# 10 - Referencia de la API

Todas las funciones y opciones. Convención: lo público usa `snake_case`.
Para ejemplos de uso ve [01 - Inicio rápido](01-inicio-rapido.md).

```cpp
#include "nitrogen/recordnreplay/rnr.hpp"
```

## `rnr`

| Función | Descripción |
|---|---|
| `rnr::init()` | Crea las 4 tasks y engancha el módulo al ActionManager y al ProfileManager. **Llamar una vez en `initialize()`.** Las prioridades de `config()` se leen aquí. |

## `Recorder`

### Qué grabar

| Función | Devuelve | Descripción |
|---|---|---|
| `attach(Neobject& obj, const std::string& profile = "ALL")` | `bool` | Agrega un objeto a la grabación activa con ese perfil. Si ya estaba, le cambia el perfil. `false` si se está grabando o el perfil no existe. |
| `detach(Neobject& obj)` | — | Quita un objeto. No hace nada mientras se graba. |
| `attach_controller(controller_id_e_t id = E_CONTROLLER_MASTER)` | `bool` | Agrega el control `master` o `partner` (perfil `ALL`). |

### Grabar

| Función | Devuelve | Descripción |
|---|---|---|
| `start_match(const std::string& name = "")` | `bool` | Empieza un `.match`. Nombre por defecto `"match"`. `false` si falla (mira `last_error()`). |
| `start_macro(const std::string& name = "")` | `bool` | Empieza una `.macro`. Nombre por defecto `"macro"`. |
| `stop()` | — | Para y guarda. No es instantáneo: espera a `is_busy() == false`. |
| `tick()` | — | Solo con `external_clock`: llamar una vez por iteración del loop, **antes** de leer el control. El frame se muestrea en ese instante. |
| `register_action(action, state = "", args = {}, wait_prev = true)` | — | Anota una acción en la macro. Para usar dentro de una intercepción `RECORD_*`. |

### Estado

| Función | Descripción |
|---|---|
| `is_recording()` | `true` mientras graba (se vuelve `false` apenas llamas a `stop()`) |
| `is_busy()` | `true` mientras graba **o** mientras termina de guardar |
| `last_file()` | Ruta del último archivo guardado completo (vacío si ninguno) |
| `last_error()` | Último mensaje de error (texto) |
| `stats()` | Estadísticas de la grabación (ver abajo) |
| `config()` | Referencia a la configuración (ver abajo) |

### `Recorder::config()`

| Campo | Por defecto | Qué hace |
|---|---|---|
| `directory` | `"/usd/"` | Carpeta de destino |
| `period_ms` | 10 | Periodo de muestreo en ms (10 = 100 Hz) |
| `ring_bytes` | 512 KB | Colchón en RAM entre medir y escribir |
| `block_bytes` | 4096 | Tamaño de cada escritura a la SD (se agranda solo si no cabe un frame) |
| `external_clock` | `false` | `true`: el sampler no tiene reloj propio; muestrea cuando el loop del usuario llama `Recorder::tick()`. Así el frame grabado es exactamente lo que ese loop leyó en esa iteración. Si el loop deja de llamar, sigue solo cada 5 periodos. |
| `auto_match` | `false` | Grabar solo al entrar a autónomo, parar al deshabilitarse tras el driver |
| `auto_match_name` | `"match"` | Nombre de la grabación automática |
| `sampler_priority` | 13 | Prioridad del sampler (antes de `rnr::init()`) |
| `writer_priority` | 12 | Prioridad del writer (antes de `rnr::init()`) |
| `on_error` | vacío | `std::function<void(const std::string&)>` que se llama ante un error |

### `Recorder::stats()` → `Stats`

| Campo | Significado |
|---|---|
| `frames` | Frames guardados |
| `dropped_frames` | Frames perdidos porque el anillo estaba lleno (idealmente 0) |
| `blocks` | Bloques escritos |
| `events` | Eventos anotados (macro) |
| `bytes` | Bytes escritos |
| `max_write_ms` | Lo que más tardó la SD en escribir un bloque |
| `min_dt_us`, `max_dt_us` | Tiempo real mínimo y máximo entre frames |
| `duration_ms` | Duración de la grabación |

Los valores se reinician en cada `start_*` y son definitivos cuando `is_busy()` vuelve a `false`.

### Nombres de archivo

`<directory><nombre>_<NNNNN>.<match|macro>`, donde `NNNNN` son 5 dígitos hexadecimales que salen de un contador
guardado en `/usd/.runid` y que sube en cada grabación. Ejemplo: `/usd/final_0001A.match`.

## `Replayer`

| Función | Devuelve | Descripción |
|---|---|---|
| `play(const std::string& file)` | `bool` | Empieza a reproducir un `.match` o `.macro` (lo detecta por la cabecera). `false` si no pudo ni empezar; los errores de lectura llegan después por `on_error` / `last_error()`. |
| `load(const std::string& file)` | `bool` | Precarga una `.macro` en RAM. No se puede mientras se reproduce. |
| `stop()` | — | Cancela la reproducción (llama a `rest()` en los objetos). |
| `wait()` | — | Bloquea hasta que termine de reproducir. |
| `is_playing()` | `bool` | ¿Se está reproduciendo? |
| `tick()` | — | Solo con `external_clock`: llamar una vez por iteración del loop, **antes** de leer el control reproducido. El player aplica el frame de ese instante. |
| `progress()` | `double` | 0 a 1. Da 0 si no se conoce la duración (archivo sin CLEAN). |
| `bind(name, Neobject& obj)` | — | Liga los canales grabados con el nombre `name` a este objeto. |
| `last_error()` | `std::string` | Último mensaje de error |
| `config()` | `Config&` | Configuración (ver abajo) |

### Control reproducido

| Función | Descripción |
|---|---|
| `controller_active(controller_id_e_t c = MASTER)` | `true` si ese control se está reproduciendo |
| `analog(controller_analog_e_t axis, c = MASTER)` | Valor del eje grabado (-127..127) |
| `digital(controller_digital_e_t b, c = MASTER)` | ¿Estaba pulsado el botón? |

Mientras se **graba**, `analog()` y `digital()` devuelven la última muestra que entró al archivo (`controller_active()` sigue en `false`). Leer el control por aquí durante la grabación garantiza que lo que hizo el robot es exactamente lo que quedó grabado.

### `Replayer::config()`

| Campo | Por defecto | Qué hace |
|---|---|---|
| `interp` | `Interp::LINEAR` | `HOLD`, `LINEAR` o `CUBIC` |
| `ema` | 1.0 | Filtro de suavizado (1 = sin filtro) |
| `speed` | 1.0 | Escala de tiempo |
| `loop` | `false` | Repetir al terminar |
| `period_ms` | 10 | Periodo de aplicación de valores / revisión de eventos |
| `external_clock` | `false` | `true`: un `.match` avanza cuando el loop del usuario llama `Replayer::tick()`, no en un reloj propio. Con `HOLD` y el mismo periodo que la grabación, el frame k sale en la iteración k. |
| `ring_bytes` | 256 KB | Lectura adelantada de un `.match` |
| `wait_timeout_ms` | 15000 | Tope de espera de `WAIT_PREV` |
| `player_priority` | 13 | Prioridad (antes de `rnr::init()`) |
| `reader_priority` | 12 | Prioridad (antes de `rnr::init()`) |
| `on_finish` | vacío | Se llama al terminar o cancelar |
| `on_error` | vacío | Se llama ante un error |

## `NORNRDATA` y `Neobject`

Ver [05 - Crear un Neobject](05-crear-un-neobject.md).

| Función | Obligatoria | Descripción |
|---|---|---|
| `profile_count()` | sí | Cantidad de perfiles |
| `profile_name(p)` | sí | Nombre del perfil `p` |
| `describe(p, out, max)` | sí | Llena la lista de canales; devuelve cuántos |
| `sample(p, out)` | sí | Lee los valores actuales |
| `apply(p, in)` | no | Aplica valores (reproducir) |
| `rest(p)` | no | Deja el objeto quieto |
| `Neobject::name()` | sí | Nombre único y estable |
| `Neobject::rnr()` | sí | Devuelve su `NORNRDATA` |

## `ActionManager` (relevante para macros)

Definido en [iron.hpp](../../include/nitrogen/heliosmgr/iron.hpp).

| Función | Descripción |
|---|---|
| `setRecordSink(fn)` | Función a la que `trigger` avisa de cada acción. El Recorder la instala en `rnr::init()`. |
| `setRecording(bool)` | Activa/desactiva el envío de acciones al sink. Lo hace el Recorder en `start_macro`/`stop`. |
| `trigger(nombre, fase, args)` | Lanza una acción en una fase (DOWN/PRESSING/UP). Es lo que se graba en las macros. |
| `execute(nombre, estado, args)` | Ejecuta una acción directamente. El Replayer usa esta; no se graba. |

## `ProfileManager` (relevante para el control reproducido)

Definido en [carbon.hpp](../../include/nitrogen/heliosmgr/carbon.hpp).

| Función | Descripción |
|---|---|
| `setInputSource(fn)` | Función que entrega un control alternativo. El Replayer la instala en `rnr::init()`. |

## Tipos de apoyo

| Tipo | Valores |
|---|---|
| `rnr::ChannelFlag` | `APPLYABLE` (1), `ANGLE` (2), `DIGITAL` (4) |
| `Replayer::Interp` | `HOLD`, `LINEAR`, `CUBIC` |
| `rnr::codec::EventFlag` | `WAIT_PREV` (1), `INTERCEPTED` (2) |
| `ActionManager::Phase` | `DOWN` (0), `PRESSING` (1), `UP` (2) |
