# 05 - Crear un Neobject grabable

Un **Neobject** es cualquier objeto del robot que quieres poder grabar y reproducir. Solo la **interfaz** está
incluida en el proyecto (los objetos concretos, como un motor, los defines tú). Esta página explica cómo hacerlo.

## Las dos piezas

```cpp
class Neobject {                         // include/nitrogen/neobjects/neobject.hpp
public:
    virtual const char* name() const = 0;   // nombre único: identifica al objeto en los archivos
    virtual NORNRDATA& rnr() = 0;           // quién responde al Recorder/Replayer
};
```

`NORNRDATA` (de *NeobjectRecordNReplayData*, en [norndata.hpp](../../include/nitrogen/recordnreplay/norndata.hpp)):

| Función | Para qué | Quién la llama |
|---|---|---|
| `profile_count()` | Cuántos perfiles ofrece | Recorder/Replayer |
| `profile_name(p)` | Nombre de cada perfil (`"ALL"`, `"ACTUAL"`...) | Recorder/Replayer |
| `describe(p, out, max)` | Lista los canales del perfil `p` | Recorder, al empezar |
| `sample(p, out)` | Lee los valores actuales | Recorder, cada frame |
| `apply(p, in)` | Aplica valores (reproducir) | Replayer, cada periodo |
| `rest(p)` | Dejar el objeto quieto/seguro | Replayer, al terminar o cancelar |

`apply` y `rest` son opcionales: por defecto no hacen nada, y el objeto simplemente "solo se graba, no se reproduce".

Cada canal se describe con:

```cpp
struct ChannelDesc {
    const char* name;   // "pos", "vel_target"...
    int8_t exp10;       // valor guardado = round(real * 10^exp10)
    uint8_t every;      // 1 = cada frame; 50 = uno de cada 50
    uint8_t flags;      // rnr::APPLYABLE | rnr::ANGLE | rnr::DIGITAL
};
```

| Flag | Significa |
|---|---|
| `rnr::APPLYABLE` | `apply()` sabe reproducir este canal |
| `rnr::ANGLE` | Es un ángulo en grados × 10^exp10; al interpolar toma el camino corto (359° → 1° pasa por 0°) |
| `rnr::DIGITAL` | Es un on/off o una máscara de bits; nunca se interpola, se mantiene |

## Ejemplo completo: un motor

Este código está **compilado** con el toolchain ARM de PROS (solo comprobación de sintaxis; no probado en un robot).

```cpp
#include "main.h"
#include "nitrogen/recordnreplay/rnr.hpp"
#include <algorithm>
#include <cmath>

// Un motor grabable. Perfiles: ALL, TARGET, ACTUAL, ACCEL.
class NeoMotor : public Neobject, public NORNRDATA {
public:
    enum Profile : uint8_t { ALL, TARGET, ACTUAL, ACCEL, COUNT };

    NeoMotor(const char* name, int8_t port) : id(name), motor(port) {}

    const char* name() const override { return id; }
    NORNRDATA& rnr() override { return *this; }

    uint8_t profile_count() const override { return COUNT; }
    const char* profile_name(uint8_t p) const override {
        static const char* names[COUNT] = {"ALL", "TARGET", "ACTUAL", "ACCEL"};
        return p < COUNT ? names[p] : "";
    }

    // Cada valor es un entero: valor_guardado = valor_real * 10^exp10
    uint8_t describe(uint8_t p, ChannelDesc* out, uint8_t max) const override {
        ChannelDesc d[5];
        uint8_t n = 0;
        switch (p) {
        case ALL:
            d[n++] = {"pos", 2, 1, rnr::APPLYABLE};         // grados * 100
            d[n++] = {"vel", 1, 1, rnr::APPLYABLE};         // rpm * 10
            d[n++] = {"vel_target", 0, 1, rnr::APPLYABLE};  // rpm
            d[n++] = {"volt", 0, 1, 0};                     // mV
            d[n++] = {"temp", 0, 50, 0};                    // C, uno de cada 50 frames
            break;
        case TARGET: d[n++] = {"vel_target", 0, 1, rnr::APPLYABLE}; break;
        case ACTUAL:
            d[n++] = {"pos", 2, 1, rnr::APPLYABLE};
            d[n++] = {"vel", 1, 1, rnr::APPLYABLE};
            break;
        case ACCEL: d[n++] = {"accel", 1, 1, 0}; break;     // rpm/s * 10
        }
        n = std::min(n, max);
        for (uint8_t i = 0; i < n; i++) out[i] = d[i];
        return n;
    }

    // Corre cada periodo: solo leer sensores, nada de prints ni delays
    void sample(uint8_t p, int32_t* out) override {
        switch (p) {
        case ALL:
            out[0] = std::lround(motor.get_position() * 100);
            out[1] = std::lround(motor.get_actual_velocity() * 10);
            out[2] = motor.get_target_velocity();
            out[3] = motor.get_voltage();
            out[4] = std::lround(motor.get_temperature());
            break;
        case TARGET: out[0] = motor.get_target_velocity(); break;
        case ACTUAL:
            out[0] = std::lround(motor.get_position() * 100);
            out[1] = std::lround(motor.get_actual_velocity() * 10);
            break;
        case ACCEL: {
            uint64_t now = pros::micros();
            double vel = motor.get_actual_velocity();
            out[0] = lastT ? std::lround((vel - lastVel) / ((now - lastT) / 1e6) * 10) : 0;
            lastVel = vel;
            lastT = now;
            break;
        }
        }
    }

    // Replay: recibe los valores ya interpolados, en el orden de describe()
    void apply(uint8_t p, const int32_t* in) override {
        switch (p) {
        case ALL: // el motor repite la velocidad pedida
        case TARGET: motor.move_velocity(in[p == ALL ? 2 : 0]); break;
        case ACTUAL: motor.move_absolute(in[0] / 100.0, 200); break; // va a la posicion grabada
        default: break; // ACCEL es solo telemetria
        }
    }

    void rest(uint8_t) override { motor.move_velocity(0); }

private:
    const char* id;
    pros::Motor motor;
    double lastVel = 0;
    uint64_t lastT = 0;
};

NeoMotor brazo("brazo", 7);

void initialize() {
    rnr::init();
    Recorder::attach_controller();
    Recorder::attach(brazo, "ACTUAL");   // de "brazo" se graba posición y velocidad reales
}
```

## Reglas importantes

### `describe`
- Para un mismo perfil debe devolver **siempre lo mismo**: el archivo guarda esta lista al inicio y se compara al reproducir.
- Devuelve cuántos canales llenó, como máximo `max`.
- Pon `every` mayor que 1 a los datos lentos (temperatura, batería) para que no gasten espacio ni CPU.

### `sample`
- Se llama **cada periodo** (por defecto cada 10 ms) desde una task de prioridad alta.
- Solo debe leer sensores. **Nada** de `printf`, `delay`, ni reservar memoria.
- Escribe en `out` los valores en el **mismo orden** que `describe`.
- Si un canal tiene `every = 50`, igual debes llenar su posición en `out` cada vez; el Recorder simplemente lo ignorará
  los frames en que no le toque.

### `apply`
- Recibe los valores ya interpolados y suavizados, en el orden de `describe`.
- Solo se llama con los perfiles que se grabaron.
- Si tu objeto no puede reproducir (por ejemplo un sensor), **no la implementes**.

### `rest`
- Es la red de seguridad: se llama cuando termina o se cancela la reproducción. Deja aquí el objeto en un estado
  seguro (un motor en 0, un pistón sin presión, etc.).

### `name`
- Debe ser **único** entre todos tus objetos y **estable**: es lo que une el objeto con sus datos en el archivo.
  Si lo cambias, los `.match` antiguos dejan de encontrarlo (usa `Replayer::bind` para el cambio).

## Elegir las unidades (`exp10`)

Decide cuántos decimales te importan y multiplica:

| Magnitud | Resolución útil | `exp10` | Ejemplo |
|---|---|---|---|
| Grados de un motor | 0.01° | 2 | 90.5° → 9050 |
| rpm | 0.1 rpm | 1 | 187.5 → 1875 |
| Voltaje en mV | 1 mV | 0 | 11800 → 11800 |
| Temperatura en °C | 1 °C | 0 | 34 → 34 |

Cuidado con el rango: un entero de 32 bits llega a unos ±2 147 millones. Con `exp10 = 2`, eso son unos
±21 millones de grados: sobra para una posición de motor.

## Diseñar buenos perfiles

- **Un perfil por uso**: uno liviano para reproducir (`ACTUAL`), uno completo para analizar (`ALL`).
- Mientras menos canales, más barato (CPU y espacio). El máximo es **255 canales en total** entre todos los
  objetos attacheados.
- Un mismo objeto solo tiene un perfil activo a la vez en una grabación.

## Cómo se ve un Neobject que ya viene incluido

El control (`master` / `partner`) está implementado en [recorder.hpp](../../include/nitrogen/recordnreplay/recorder.hpp)
(clase `ControllerObject`). Es un buen ejemplo mínimo: un perfil, cinco canales, y un `apply` que escribe en el
"control virtual" en vez de mover un motor.
