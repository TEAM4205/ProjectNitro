# Record'n'Replay de Nitrogen

Guía completa del módulo que **graba** lo que pasa en el robot y lo **reproduce** después.
Todo el código está en [`include/nitrogen/recordnreplay/`](../../include/nitrogen/recordnreplay/).

## ¿Para qué sirve?

| Quiero... | Uso | Archivo que se crea |
|---|---|---|
| Guardar un partido completo (control, motores, sensores) para verlo o repetirlo | **Modo activo** | `.match` |
| Guardar una secuencia de acciones (macro, autónomo) para repetirla | **Modo pasivo** | `.macro` |

Los dos modos usan los mismos dos "puntos de entrada" para el programador:

```cpp
Recorder::...   // grabar
Replayer::...   // reproducir
```

## Las dos ideas en una frase cada una

- **Modo activo (`.match`)**: cada cierto tiempo (por defecto cada 10 ms) el Recorder le pregunta a cada objeto
  "¿cuánto valen tus datos ahora?" y lo guarda. Es como filmar el robot con una cámara de datos.
- **Modo pasivo (`.macro`)**: el Recorder no mira el robot; solo anota **qué acciones se pidieron y cuándo**
  ("a los 2.3 s, ejecuta `moverAutonomo(40, 0)`"). Es como escribir una receta.

## Cómo leer esta documentación

Si es tu primera vez, lee en este orden:

1. [01 - Inicio rápido](01-inicio-rapido.md): ejemplos para copiar y pegar.
2. [02 - Arquitectura](02-arquitectura.md): qué piezas hay y cómo se hablan entre sí.
3. [03 - Modo activo](03-modo-activo-match.md) y [04 - Modo pasivo](04-modo-pasivo-macro.md).

Luego, según lo que necesites:

| Documento | De qué trata |
|---|---|
| [05 - Crear un Neobject](05-crear-un-neobject.md) | Cómo hacer que un motor/sensor tuyo sea grabable (con código compilado) |
| [06 - Formato `.match`](06-formato-match.md) | Cómo es el archivo por dentro, byte por byte |
| [07 - Formato `.macro`](07-formato-macro.md) | Ídem para las macros |
| [08 - Compresión](08-compresion.md) | Cómo caben tantos datos en tan pocos bytes |
| [09 - Reproducción y suavizado](09-reproduccion-y-suavizado.md) | Cómo se reconstruye el movimiento al reproducir |
| [10 - Referencia de la API](10-referencia-api.md) | Todas las funciones y opciones, una por una |
| [11 - Herramientas y pruebas](11-herramientas-y-pruebas.md) | Visor en Python, tests, archivo de ejemplo |
| [12 - Problemas y límites](12-problemas-y-limites.md) | Mensajes de error, límites y cosas que aún no están verificadas |

## Glosario rápido

| Palabra | Significa |
|---|---|
| **Neobject** | Cualquier objeto del robot que se puede grabar (un motor, un sensor...). Es la clase base. |
| **NORNRDATA** | La parte del Neobject que dice **qué** datos entrega para grabar y **cómo** se reproducen. |
| **Perfil** | Una "receta" de qué datos de un objeto se graban (ej.: de un motor, solo la posición). |
| **Canal** | Un valor individual dentro de un perfil (ej.: `pos`, `vel`). Siempre es un número entero. |
| **Frame** | Una "foto" de todos los canales en un instante. |
| **Bloque** | Un grupo de frames (~4 KB) que se escribe de una vez a la SD. |
| **Keyframe** | El primer frame de un bloque: guarda los valores completos, no diferencias. |
| **Anillo (ring)** | Memoria intermedia que une la task que graba con la que escribe a la SD. |
| **Evento** | Una línea de una macro: "ejecuta tal acción con tales argumentos en tal momento". |
| **Intercepción** | Código `extends RECORD_*` de una acción que decide qué se graba en la macro. |

## Mapa del código

```
include/nitrogen/recordnreplay/
  rnr.hpp          Punto de entrada: rnr::init(). Incluye todo lo demás.
  recorder.hpp     namespace Recorder: grabar .match y .macro
  replayer.hpp     namespace Replayer: reproducir .match y .macro
  norndata.hpp     La interfaz NORNRDATA que implementa cada Neobject
  format.hpp       Estructura binaria de los archivos (cabecera, tabla, CRC)
  codec.hpp        Compresión de frames y de eventos
  smoothing.hpp    Interpolación y filtro para reproducir
  ring.hpp         Anillo de bloques entre dos tasks, sin mutex

include/nitrogen/neobjects/neobject.hpp   Clase base Neobject
include/nitrogen/heliosmgr/iron.hpp       ActionManager (conecta el modo pasivo)
include/nitrogen/heliosmgr/carbon.hpp     ProfileManager (recibe el control grabado al reproducir)
include/nitrogen/Storage.hpp              Acceso a la SD (se usa para abrir/cerrar archivos)

3rd_party/match_viewer/                   Visor de .match en Python
tests/rnr_codec_test.cpp                  Test del formato y la compresión (corre en el PC)
tests/make_sample_match.cpp               Genera un .match de ejemplo
```
