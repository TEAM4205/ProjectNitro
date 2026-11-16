#pragma once
// ============================================================================
//  norndata.hpp  -  NORNRDATA (NeobjectRecordNReplayData): lo que cada Neobject implementa
//  para que el Recorder lo grabe en modo activo (.match) y el Replayer lo reproduzca.
//
//  Cada objeto ofrece uno o mas PERFILES (que datos grabar). Cada perfil es una lista fija de
//  CANALES (valores enteros). Ejemplo de un motor:
//
//    perfil    canales                               apply() en el replay
//    ALL       pos, vel, vel_target, volt, cur, temp  move_velocity(vel_target)
//    TARGET    vel_target                             move_velocity(vel_target)
//    ACTUAL    pos, vel                               move_absolute(pos, |vel|): sigue el camino real
//    ACCEL     accel                                  (no reproducible: solo telemetria)
//
//    uint8_t describe(uint8_t p, ChannelDesc* out, uint8_t max) const override {
//        if (p == ACTUAL) { out[0] = {"pos", 2, 1, rnr::APPLYABLE};      // grados * 100
//                           out[1] = {"vel", 1, 1, rnr::APPLYABLE};      // rpm * 10
//                           return 2; }
//        ...
//    }
//    void sample(uint8_t p, int32_t* out) override {
//        if (p == ACTUAL) { out[0] = lround(m.get_position() * 100); out[1] = lround(m.get_actual_velocity() * 10); }
//    }
//
//  Reglas:
//    * Valores en PUNTO FIJO: guardado = round(real * 10^exp10). El codec solo ve enteros.
//    * sample() corre en la task de muestreo cada periodo: solo leer sensores, nada de
//      prints, delays ni allocations.
//    * describe() debe devolver siempre lo mismo para un perfil (el archivo lo guarda al inicio).
//    * Un perfil se identifica por su NOMBRE en el archivo: se puede reordenar sin romper .match viejos.
// ============================================================================

#include "format.hpp"
#include <cstdint>

struct ChannelDesc {
    const char* name;
    int8_t exp10 = 0;  // valor guardado = round(real * 10^exp10)
    uint8_t every = 1; // decimacion: 1 = cada frame, 50 = uno de cada 50 (temperatura, bateria)
    uint8_t flags = 0; // rnr::APPLYABLE | rnr::ANGLE | rnr::DIGITAL
};

class NORNRDATA {
public:
    virtual ~NORNRDATA() = default;

    virtual uint8_t profile_count() const = 0;
    virtual const char* profile_name(uint8_t profile) const = 0;
    // Llena out con los canales del perfil y devuelve cuantos son (como mucho max)
    virtual uint8_t describe(uint8_t profile, ChannelDesc* out, uint8_t max) const = 0;
    // Lee los valores actuales en el orden de describe()
    virtual void sample(uint8_t profile, int32_t* out) = 0;
    // Replay: aplica valores (ya interpolados y suavizados). Por defecto el objeto no reproduce.
    virtual void apply(uint8_t profile, const int32_t* in) { (void)profile; (void)in; }
    // Replay terminado o cancelado: dejar el objeto quieto / seguro
    virtual void rest(uint8_t profile) { (void)profile; }
};
