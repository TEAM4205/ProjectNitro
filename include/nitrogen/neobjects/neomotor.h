#pragma once
// ============================================================================
//  neomotor.h  -  NeoMotor: un pros::MotorGroup (o un solo motor) que ademas se graba.
//  Se usa igual que un MotorGroup; lo unico que agrega es el Record'n'Replay.
//
//    NeoMotor left_mg("left", {-4, -21});
//    Recorder::attach(left_mg);                // en initialize(), despues de rnr::init()
//
//  Perfil "ALL": la posicion de cada motor (unidades del encoder * 100), un canal por motor.
//  Replay: cada motor repite su camino RELATIVO a donde esta al empezar (move_absolute, el PID
//  interno del motor), asi que no hace falta resetear encoders. Mientras se reproduce, que nadie
//  mas mande comandos a estos motores.
// ============================================================================

#include "api.h"
#include "nitrogen/neobjects/neobject.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

class NeoMotor : public pros::MotorGroup, public Neobject, public NORNRDATA {
public:
    NeoMotor(const char* name, std::initializer_list<std::int8_t> ports)
        : pros::MotorGroup(ports), id(name), portList(ports), last(portList.size()), base(portList.size()), rpm(portList.size()) {
        for (auto p : portList) chName.push_back("pos" + std::to_string(p));
    }

    const char* name() const override { return id; }
    NORNRDATA& rnr() override { return *this; }

    uint8_t profile_count() const override { return 1; }
    const char* profile_name(uint8_t) const override { return "ALL"; }

    uint8_t describe(uint8_t, ChannelDesc* out, uint8_t max) const override {
        uint8_t n = uint8_t(std::min<size_t>(portList.size(), max));
        for (uint8_t i = 0; i < n; i++) out[i] = {chName[i].c_str(), 2, 1, rnr::APPLYABLE};
        return n;
    }

    void sample(uint8_t, int32_t* out) override {
        for (size_t i = 0; i < portList.size(); i++) {
            double p = pros::c::motor_get_position(portList[i]);
            if (std::isfinite(p)) last[i] = int32_t(std::lround(p * 100)); // desconectado: repite el ultimo
            out[i] = last[i];
        }
    }

    void apply(uint8_t, const int32_t* in) override {
        if (!replaying) { // primer frame: el camino grabado arranca donde esta cada motor ahora
            for (size_t i = 0; i < portList.size(); i++) {
                base[i] = pros::c::motor_get_position(portList[i]) - in[i] / 100.0;
                int g = pros::c::motor_get_gearing(portList[i]);
                rpm[i] = g == pros::E_MOTOR_GEARSET_36 ? 100 : g == pros::E_MOTOR_GEARSET_06 ? 600 : 200;
            }
            replaying = true;
        }
        for (size_t i = 0; i < portList.size(); i++)
            pros::c::motor_move_absolute(portList[i], base[i] + in[i] / 100.0, rpm[i]);
    }

    void rest(uint8_t) override {
        replaying = false;
        brake();
    }

private:
    const char* id;
    std::vector<std::int8_t> portList;
    std::vector<std::string> chName;
    std::vector<int32_t> last;
    std::vector<double> base;
    std::vector<int32_t> rpm;
    bool replaying = false;
};
