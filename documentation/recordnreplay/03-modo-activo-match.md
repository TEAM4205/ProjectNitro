# 03 - Modo activo (`.match`)

Graba el estado de los objetos del robot a intervalos regulares. Sirve para guardar partidos completos,
analizarlos después y también reproducirlos.

## La idea

```
        cada 10 ms
 ┌─────────────────────────────────────────┐
 │  Recorder pregunta a cada objeto:       │
 │    "¿cuánto valen tus canales ahora?"   │      →  un FRAME  →  se comprime  →  .match
 └─────────────────────────────────────────┘
```

Un objeto grabable se llama **Neobject**. Dentro tiene un **NORNRDATA**, que es quien responde a las preguntas del
Recorder. Un mismo objeto puede ofrecer varios **perfiles** (conjuntos de datos), y tú eliges cuál grabar.

## Conceptos

### Canal

Un número entero que cambia con el tiempo. Ejemplos: `pos` (posición de un motor), `lx` (stick izquierdo en X).
Todos los canales son enteros de 32 bits. Para guardar decimales se usa **punto fijo**: se multiplica por una
potencia de 10 (`exp10`):

| Real | exp10 | Se guarda |
|---|---|---|
| 12.34° | 2 | 1234 |
| 187.5 rpm | 1 | 1875 |
| 34 °C | 0 | 34 |

### Perfil

Un perfil es la lista de canales que se graban de un objeto. Elegir el perfil adecuado ahorra espacio y CPU.
Ejemplo de un motor:

| Perfil | Canales | Para qué |
|---|---|---|
| `ALL` | pos, vel, vel_target, volt, temp | Análisis completo |
| `TARGET` | vel_target | Reproducir lo que se le ordenó |
| `ACTUAL` | pos, vel | Reproducir el movimiento real |
| `ACCEL` | accel | Solo ver aceleraciones |

Los perfiles los define el programador de cada objeto (ver [05](05-crear-un-neobject.md)).

### Decimación (`every`)

Cada canal indica cada cuántos frames se muestrea. Un canal con `every = 1` se mide siempre; uno con
`every = 50` se mide una vez cada 50 frames (a 100 Hz, 2 veces por segundo). Es ideal para datos lentos como
la temperatura o la batería. Entre mediciones el canal conserva su último valor y **no ocupa nada** en el archivo.

## El control como objeto

El control es un Neobject especial que ya viene incluido. Hay dos, creados en `rnr::init()`:

| Nombre | Control |
|---|---|
| `master` | El control principal |
| `partner` | El control del compañero |

Cada uno ofrece un perfil `ALL` con 5 canales:

| Canal | Valor |
|---|---|
| `lx`, `ly` | Stick izquierdo, de -127 a 127 |
| `rx`, `ry` | Stick derecho, de -127 a 127 |
| `buttons` | Máscara de 12 bits: bit 0 = L1, L2, R1, R2, UP, DOWN, LEFT, RIGHT, X, B, Y, bit 11 = A |

```cpp
Recorder::attach_controller();                          // master
Recorder::attach_controller(pros::E_CONTROLLER_PARTNER); // partner
```

## Paso a paso: qué pasa al grabar

1. **`start_match("final")`** comprueba: módulo iniciado, no se está grabando ya, hay SD, hay objetos attacheados.
   Si algo falla devuelve `false` y deja el motivo en `Recorder::last_error()`.
2. **Construye la tabla de canales:** le pide a cada objeto attacheado su lista (`describe`). Si en total pasa de
   255 canales, falla. Esa tabla se escribirá al inicio del archivo.
3. **Reserva el anillo** (512 KB por defecto) y calcula el tamaño de los bloques (mínimo lo configurado, más grande
   si el peor frame no cupiera).
4. **Pone nombre** `final_0000N.match` en `directory`, con un número que sube solo.
5. **Despierta a las tasks.** El *writer* abre el archivo y escribe cabecera y tabla; el *sampler* empieza a medir.
6. **Cada `period_ms`**, el sampler:
   - anota el tiempo real (`micros()`),
   - llama a `sample()` de cada objeto,
   - codifica el frame directamente dentro de un bloque del anillo.
7. **Cuando un bloque se llena**, lo entrega al *writer* y empieza otro. El *writer* hace **un solo `fwrite`
   por bloque** y cede la CPU 1 ms entre bloques.
8. **Si el anillo se llena** (la SD se atrasó mucho), los frames de ese momento se descartan y se cuentan en
   `dropped_frames`. El siguiente bloque trae su propio tiempo, así que el hueco queda marcado y al reproducir
   se interpola por encima.
9. **`stop()`** hace que el sampler termine; el *writer* vacía todo lo pendiente, reescribe la cabecera con el total
   de frames y la duración y marca el archivo como **CLEAN** (cerrado correctamente).

## Grabar automáticamente

```cpp
Recorder::config().auto_match = true;
```

Mientras el sampler está inactivo se despierta cada 20 ms a mirar el estado de la competencia:

- Empieza a grabar cuando el robot **entra a autónomo** (habilitado, modo autónomo).
- Para cuando el robot se **deshabilita después de haber pasado por el driver**.

Útil para tener siempre guardado el último partido. Requiere haber hecho los `attach` antes. Si ya hay otra grabación
en curso, no arranca.

## Reproducir un `.match`

```cpp
Replayer::play("/usd/final_00001.match");
```

Qué hace el Replayer:

1. Lee la cabecera y la tabla de canales, y las valida (CRC).
2. **Liga cada grupo de canales con el objeto de igual nombre** (`"brazo"`, `"master"`...). Si en este programa
   no existe ese objeto (o su perfil cambió), esos canales se **ignoran** y se avisa por `on_error`; el resto
   se reproduce igual.
3. Arranca el *reader*, que va metiendo bloques en un anillo.
4. Cada `Replayer::config().period_ms`, calcula el valor de cada canal en ese instante exacto
   (interpolando entre frames grabados, ver [09](09-reproduccion-y-suavizado.md)) y llama a `apply()` del objeto.
5. Al terminar, o al cancelar, llama a `rest()` en todos los objetos para dejarlos quietos.

### El control grabado

El control no mueve nada por sí mismo; es un dato más. Al reproducir, sus valores se guardan en un "control virtual":

```cpp
Replayer::controller_active();                // true mientras haya un control reproduciéndose
Replayer::analog(ANALOG_LEFT_Y);              // valor grabado del stick
Replayer::digital(DIGITAL_A);                 // ¿estaba pulsado el botón?
```

Además, el `ProfileManager` (carbon) usa automáticamente ese control virtual en lugar del real, así que los
perfiles reaccionan a lo grabado igual que reaccionarían al control de verdad.

> Mientras se reproduce, los perfiles **no ven** el control real. Por eso, el botón que cancela la reproducción
> conviene leerlo directamente del `pros::Controller` (como hace `main.cpp`), no a través de un perfil.

### Ligar por nombre

Un `.match` guarda el **nombre** de cada objeto, no su posición. Así puedes reordenar los `attach` o cambiar el
código, y los archivos viejos siguen funcionando. Si renombraste un objeto, dile al Replayer cuál es el equivalente:

```cpp
Replayer::bind("brazo", brazo_nuevo);   // los canales grabados como "brazo" van a brazo_nuevo
```

## Qué mirar después de grabar

```cpp
auto s = Recorder::stats();
// s.frames           frames guardados
// s.dropped_frames   frames perdidos (deberían ser 0)
// s.blocks           bloques escritos
// s.bytes            tamaño del archivo
// s.max_write_ms     lo más que tardó la SD en escribir un bloque
// s.min_dt_us, s.max_dt_us   tiempo real mínimo y máximo entre frames
// s.duration_ms      duración de la grabación
```

Si `max_write_ms` se acerca a la cantidad de ms que cubre tu anillo, o `dropped_frames` > 0, sube `ring_bytes`
o graba menos canales.
