# 01 - Inicio rápido

Cinco minutos para grabar y reproducir. Todo el código de esta página es el que ya usa [`src/main.cpp`](../../src/main.cpp).

## Antes de empezar

- Pon una **tarjeta SD** en el Brain (FAT32). Sin SD, grabar devuelve `false` y deja un error en `last_error()`.
- Incluye el módulo una vez:

```cpp
#include "nitrogen/recordnreplay/rnr.hpp"
```

## Paso 1: iniciar el módulo (una sola vez)

En `initialize()`. Es obligatorio: ahí nacen las tasks del módulo, y deben nacer ahí porque las tasks de
competencia (`autonomous`, `opcontrol`) **mueren** al cambiar de modo, y no queremos que eso corte una
escritura a la SD a medias.

```cpp
void initialize() {
    rnr::init();                    // crea las tasks (una vez)
    Recorder::attach_controller();  // "quiero grabar el control master"
}
```

## Paso 2: grabar un partido (`.match`)

```cpp
Recorder::start_match("drive");   // empieza. Crea /usd/drive_00001.match
// ... el robot hace lo suyo ...
Recorder::stop();                 // para y guarda
```

- Mientras graba, el robot funciona normal; la grabación ocurre en segundo plano.
- `stop()` no es instantáneo: el archivo termina de guardarse unos milisegundos después.
  Para saber cuándo está listo mira `Recorder::is_busy()` (vuelve a `false` al terminar).
- El archivo queda con el nombre que diste + un número que sube solo (`drive_00001`, `drive_00002`...), así
  que **nunca se sobrescribe** una grabación anterior.

## Paso 3: reproducirlo

```cpp
Replayer::play(Recorder::last_file());   // ruta del último archivo guardado completo
Replayer::wait();                         // opcional: espera a que termine
```

`play` detecta solo si es un `.match` o un `.macro` mirando la cabecera del archivo. También puedes pasarle
una ruta fija: `Replayer::play("/usd/drive_00001.match")`.

Cuando se reproduce un `.match` con el control grabado, tu código lee los sticks grabados en lugar de los reales:

```cpp
if (Replayer::controller_active())
    moveTank(Replayer::analog(ANALOG_LEFT_Y), Replayer::analog(ANALOG_RIGHT_Y));
else
    moveTank(master.get_analog(ANALOG_LEFT_Y), master.get_analog(ANALOG_RIGHT_Y));
```

## Ejemplo completo con botones

Y graba/para, X reproduce/cancela:

```cpp
void opcontrol() {
    pros::Controller master(pros::E_CONTROLLER_MASTER);
    while (true) {
        if (master.get_digital_new_press(DIGITAL_Y)) {
            if (Recorder::is_recording()) Recorder::stop();
            else Recorder::start_match("drive");
        }
        if (master.get_digital_new_press(DIGITAL_X)) {
            if (Replayer::is_playing()) Replayer::stop();
            else if (!Recorder::is_busy()) Replayer::play(Recorder::last_file());
        }
        // ... manejar el robot ...
        pros::delay(20);
    }
}
```

> El `!Recorder::is_busy()` evita reproducir el archivo **anterior** si pulsas X justo después de parar,
> cuando el nuevo todavía se está guardando.

## Grabar más cosas que el control

Cualquier `Neobject` se puede agregar con uno de sus perfiles (qué datos guardar de él):

```cpp
Recorder::attach(brazo, "ACTUAL");   // de "brazo", guarda posición y velocidad reales
```

Hazlo antes de `start_match` (no se puede mientras se graba). Ver [05 - Crear un Neobject](05-crear-un-neobject.md)
para hacer que tus objetos sean grabables.

## Grabar automáticamente el partido

Si quieres que se grabe solo, sin pulsar nada:

```cpp
Recorder::config().auto_match = true;
Recorder::config().auto_match_name = "match";
```

Empieza al entrar al autónomo y para cuando el robot se deshabilita después del driver. Los `attach` los
tienes que haber hecho antes (en `initialize`).

## Grabar una macro (`.macro`)

```cpp
Recorder::start_macro("auton_izq");   // desde aquí se anotan las acciones que se piden
// ... manejas el robot y se van anotando las acciones ...
Recorder::stop();                      // guarda /usd/auton_izq_00001.macro
```

Y para repetirla, por ejemplo en `autonomous()`:

```cpp
void autonomous() {
    Replayer::play("/usd/auton_izq_00001.macro");
    Replayer::wait();
}
```

> **Importante:** una macro solo captura las acciones que pasan por `ActionManager::trigger`. Ver
> [04 - Modo pasivo](04-modo-pasivo-macro.md).

Para que el autónomo arranque sin leer la SD en ese momento, precarga la macro en `initialize()`:

```cpp
Replayer::load("/usd/auton_izq_00001.macro");   // la deja en RAM; play() la usa directamente
```

## Mirar el resultado

- En el PC, con el visor: `python 3rd_party/match_viewer/match_viewer.py /ruta/drive_00001.match`
  (ver [11 - Herramientas](11-herramientas-y-pruebas.md)).
- En el robot, después de grabar: `Recorder::stats()` te dice cuántos frames se grabaron, cuántos se perdieron
  y cuánto tardó la SD en el peor caso.

## Lista de comprobación

- [ ] `rnr::init()` está en `initialize()`.
- [ ] Hay SD en el Brain.
- [ ] Hiciste `attach` / `attach_controller` **antes** de `start_match`.
- [ ] Después de `stop()` esperas a que `is_busy()` sea `false` antes de reproducir.
- [ ] Revisas `Recorder::stats().dropped_frames` (debería ser 0) y `Recorder::last_error()` (debería estar vacío).
