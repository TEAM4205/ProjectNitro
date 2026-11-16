# 02 - Arquitectura

Cómo está armado el módulo por dentro y por qué.

## Vista general

```
                         ┌──────────────────────────────────────────────┐
                         │                 TU PROGRAMA                  │
                         │  Recorder::start_match / stop / attach ...   │
                         │  Replayer::play / stop / wait ...            │
                         └───────────────┬──────────────┬───────────────┘
                                         │              │
              ┌──────────────────────────▼───┐      ┌───▼───────────────────────────┐
              │          RECORDER            │      │           REPLAYER            │
              │  (recorder.hpp)              │      │  (replayer.hpp)               │
              └──────────────────────────────┘      └───────────────────────────────┘
                 │                     │                │                      │
         ┌───────▼──────┐     ┌────────▼────────┐  ┌────▼───────┐     ┌────────▼──────┐
         │  Neobjects   │     │  ActionManager  │  │ Neobjects  │     │ ActionManager │
         │ (modo activo)│     │ (modo pasivo)   │  │ apply()    │     │ execute()     │
         └──────────────┘     └─────────────────┘  └────────────┘     └───────────────┘

         Compartido: format.hpp (estructura de archivos) · codec.hpp (compresión) ·
                     smoothing.hpp (interpolación) · ring.hpp (anillo entre tasks)
```

## Las 4 tasks

El módulo crea **cuatro tasks** en `rnr::init()` y viven todo el programa. Casi siempre están dormidas.

| Task | Prioridad | Pila | Qué hace |
|---|---|---|---|
| `rnr_sampler` | 13 | 8 KB | Mide los objetos cada `period_ms` y arma los frames |
| `rnr_writer` | 3 | 16 KB | Es la **única** que escribe a la SD al grabar |
| `rnr_player` | 13 | 8 KB | Reproduce: calcula los valores y los aplica a tiempo |
| `rnr_reader` | 3 | 16 KB | Es la **única** que lee la SD al reproducir |

(Las pilas se miden en palabras de 4 bytes en PROS: 0x800 palabras = 8 KB, 0x1000 = 16 KB.)

### ¿Por qué esas prioridades?

- En PROS, mayor número = más prioridad. Tu `opcontrol` y `autonomous` corren en 8.
- El **daemon** de PROS (el que habla con VEXos) corre en prioridad 14, así que siempre tiene preferencia sobre
  nosotros. Las tasks de **tiempo** (sampler y player) van en 13: justo debajo, para que ni tu código ni la SD las atrasen.
- Las tasks de **SD** (writer y reader) van en 3: la SD es lenta e impredecible (puede tardar 200 ms o más en
  una escritura), y no debe quitarle CPU a nada importante. Si algo debe esperar, que sea ella.

### ¿Por qué se crean en `initialize()`?

Las tasks de competencia se **borran** cada vez que cambia el estado (de autónomo a driver, por ejemplo). Si el
grabador viviera ahí, el cambio podría matarlo en medio de un `fwrite` y dejar un archivo roto. Creadas en
`initialize()`, las tasks del módulo sobreviven a todos los cambios.

## Separar "medir" de "escribir"

Es la decisión más importante del diseño.

```
  sampler (prio 13)                                        writer (prio 12)
  ─────────────────                                        ───────────────
  cada 10 ms exactos:                                      cuando hay un bloque listo:
    leer objetos                                             fwrite (un bloque de 4 KB)
    codificar el frame  ───►   ANILLO DE BLOQUES   ───►      pausa de 1 ms
    (cero copias)             (512 KB en RAM)                a la tarjeta SD
```

La SD puede congelarse unos 200 ms de vez en cuando (es normal en tarjetas SD). Si el mismo código que mide
también escribiera, esos 200 ms se perderían como 20 muestras. Con el anillo en medio, el sampler sigue
midiendo y los bloques se acumulan en RAM mientras la SD "respira". El margen depende de cuántos datos
generes: con solo el control (unos 6 bytes por frame, 600 bytes por segundo) 512 KB duran muchos minutos; con
muchos canales (200 KB por segundo) son unos 2.5 segundos, que sigue siendo bastante más que una pausa típica de la SD.

Al **reproducir** es el mismo esquema al revés: el `reader` va leyendo bloques de la SD y los deja en un anillo
(256 KB), y el `player` los consume a su ritmo sin esperar nunca a la SD.

## El anillo de bloques (`ring.hpp`)

Un anillo es una cola circular de "ranuras". Cada ranura guarda **un bloque completo**.

- Un lado (el productor) llena ranuras y avanza `head`.
- El otro (el consumidor) las vacía y avanza `tail`.
- Cada lado solo modifica **su propio** contador, así que no hace falta ningún mutex (candado).
- Se escribe y se lee **directamente en la ranura**, sin copiar datos de un sitio a otro.
- Si está lleno (`head - tail == total de ranuras`) el productor no puede escribir. Al grabar, ese frame se
  cuenta como perdido (`dropped_frames`).

La memoria se pide al empezar a grabar/reproducir y se **devuelve** al terminar.

## El tiempo

- Cada frame se marca con `pros::micros()` (microsegundos) en el instante real en que se tomó. **No** se asume
  que cada frame llega exactamente a 10 ms: VEXos a veces entrega tramas de 4 o 6 ms, y esa deriva se guarda.
- El sampler usa `delay_until`, que despierta en `t0 + 10, t0 + 20, t0 + 30...` contando desde el inicio.
  Con un `delay(10)` normal, un retraso se acumularía; con `delay_until` no.
- El reloj de PROS tiene resolución de 1 ms, por eso `period_ms` es siempre un número entero de milisegundos.

## Memoria

| Qué | Cuánto | Cuándo |
|---|---|---|
| Pilas de las 4 tasks | 48 KB | Siempre (desde `rnr::init()`) |
| Anillo del Recorder | 512 KB (configurable) | Solo mientras graba |
| Anillo del Replayer | 256 KB (configurable) | Solo mientras reproduce un `.match` |
| Macro grabándose | crece con los eventos (empieza en 16 KB) | Desde `start_macro` hasta `stop` |
| Macro precargada | tamaño del archivo | Desde `Replayer::load` |
| Estado fijo (codificador, etc.) | pocos KB | Siempre |

Según el linker script de PROS, el heap del Brain es de unos 46 MiB, así que son valores cómodos.

## Seguridad entre tasks

- `Recorder::attach` y `detach` **no** se pueden usar mientras se graba (devuelven `false` y avisan).
- Las macros usan un `Mutex` porque dos tasks pueden anotar eventos a la vez (tu `opcontrol` y una intercepción).
- Las banderas de estado (`recording`, `playing`, `stopReq`...) son `std::atomic`.
- El Recorder y el Replayer pueden estar activos **a la vez**: por ejemplo, grabar con `auto_match` el partido
  mientras una macro de autónomo se está reproduciendo.

## Cómo se conecta con el resto de Nitrogen

| Pieza | Qué hace el módulo con ella |
|---|---|
| `Neobject` / `NORNRDATA` | El Recorder le pide datos (`sample`); el Replayer le da datos (`apply`). |
| `ActionManager` ([iron.hpp](../../include/nitrogen/heliosmgr/iron.hpp)) | El Recorder se engancha a `trigger` (el "sink"); el Replayer llama a `execute`. |
| `ProfileManager` ([carbon.hpp](../../include/nitrogen/heliosmgr/carbon.hpp)) | Al reproducir un control grabado, `carbon` lee ese control en lugar del real (`setInputSource`). |
| `Storage` ([Storage.hpp](../../include/nitrogen/Storage.hpp)) | `sdcardok()`, `sdnextrunid()` (número de grabación) y `Storage::detail::File` (abrir/cerrar archivos). |

## Estructura de los espacios de nombres

Cada namespace público tiene un `detail` con las partes internas (nombres en `camelCase`); lo público usa
`snake_case`:

```
Recorder::start_match()      Recorder::detail::recordMatch()
Replayer::play()             Replayer::detail::playMatch()
rnr::init()                  rnr::detail::objects()   rnr::codec::...   rnr::fmt::...   rnr::smooth::...
```
