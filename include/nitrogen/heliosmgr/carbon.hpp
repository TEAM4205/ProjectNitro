#pragma once
// ============================================================================
//  carbon.hpp  -  ProfileManager: administrador interno de los Profiles
//
//  profiles.generated.cpp registra en profilesInitialization() los perfiles, controles,
//  rangos y dobles. Cada tick, el manager:
//    1. aplica el cambio de perfil pedido con select() (desde cualquier task),
//    2. toma UNA foto de los dos controles (botones, ejes, rangos, dobles),
//    3. llama a la funcion del perfil activo, que pregunta todo a esa foto.
//  Por eso newPress/newRelease se pueden consultar varias veces en el mismo tick
//  (el get_digital_new_press de PROS solo devuelve true a la primera llamada).
//
//  Valores de los ejes:
//    raw(c, eje)    lo que da el control, -127..127
//    value(c, eje)  pasado por su AxisConfig: deadzone -> curva -> escala -> invert -> slew
//  Rangos (STICK DOWN/UP [a, b]) se evaluan sobre raw: hablan de la posicion fisica del stick.
//
//  Si un control se desconecta, todo se lee como suelto / 0: los botones mantenidos
//  disparan su newRelease y los motores se detienen.
//
//  Registrar SOLO durante la inicializacion. Las configuraciones del perfil
//  (setAxisConfig, addToggle, addCooldown) se pueden llamar cada tick: son idempotentes.
// ============================================================================

#include "api.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct AxisConfig {
    double deadzone = 0; // |raw| <= deadzone -> 0; el resto se re-escala para seguir llegando a 127
    double curve = 1;    // exponente: 1 lineal, >1 mas fino cerca del centro
    double scale = 1;    // multiplicador final
    bool invert = false;
    double slew = 0;     // maximo cambio por tick (0 = sin limite): aceleracion suave
};

namespace ProfileManager {

using Ctrl = pros::controller_id_e_t;
using Btn = pros::controller_digital_e_t;
using Axis = pros::controller_analog_e_t;
enum Edge { DOWN, UP };

namespace detail {
constexpr int FIRST = pros::E_CONTROLLER_DIGITAL_L1; // los botones van de L1 (6) a A (17)
constexpr int BUTTONS = 12;

struct Prof { std::string name; void (*fn)(); };
struct Pad {
    bool cur[BUTTONS]{}, prev[BUTTONS]{}, connected = false;
    uint32_t downAt[BUTTONS]{};
    int raw[4]{};
    AxisConfig cfg[4];
    double out[4]{};
    uint32_t outTick[4]{};
};
struct Range { std::string id; Ctrl c; Axis a; int lo, hi; bool in = false, prevIn = false; };
struct Double { std::string id; Ctrl c; Btn b; Edge e; uint32_t last = 0; bool fired = false; };
struct Toggle { int value, states; };
struct Cooldown { uint32_t ms; uint32_t last = 0; bool used = false; };

inline std::vector<Prof> profiles;
inline std::atomic<int> current{-1}, requested{-1};
inline pros::Controller* ctrl[2] = {nullptr, nullptr};
inline Pad pad[2];
inline std::vector<Range> ranges;
inline std::vector<Double> doubles;
inline std::map<std::string, Toggle> toggles;
inline std::map<std::string, Cooldown> cooldowns;
inline std::function<void(const std::string&)> listener;
inline uint32_t now = 0, tickCount = 0, doubleWindow = 300;
inline bool showName = true, started = false;
// Fuente alternativa del control (c, ejes[4], botones como mascara L1..A). true = usar esos valores.
inline bool (*inputSource)(int c, int* raw, uint16_t* buttons) = nullptr;

inline Range* range(const std::string& id) {
    for (auto& r : ranges)
        if (r.id == id) return &r;
    printf("[carbon] rango desconocido: %s\n", id.c_str());
    return nullptr;
}

inline void cycle(int dir) {
    int n = profiles.size();
    if (n == 0) return;
    int base = requested >= 0 ? (int)requested : (int)current;
    requested = ((base + dir) % n + n) % n;
}
} // namespace detail


/* ------------------------------- REGISTRO -------------------------------- */

inline void registerProfile(const std::string& name, void (*fn)()) {
    detail::profiles.push_back({name, fn});
    if (detail::current < 0) detail::current = 0; // el primero registrado arranca activo
}

inline void registerController(pros::Controller* master, pros::Controller* partner = nullptr) {
    detail::ctrl[0] = master;
    detail::ctrl[1] = partner;
}

// STICK_X DOWN/UP [lo, hi]: entra / sale del rango (inclusivo)
inline void registerRangeListener(const std::string& id, Ctrl c, Axis a, int lo, int hi) {
    detail::ranges.push_back({id, c, a, lo, hi});
}

// BOTON DOUBLE DOWN/UP: dos flancos dentro de la ventana de doble pulsacion
inline void registerDoubleListener(const std::string& id, Ctrl c, Btn b, Edge e) {
    detail::doubles.push_back({id, c, b, e});
}

inline void setDoubleWindow(uint32_t ms) { detail::doubleWindow = ms; }
inline void showNameOnController(bool on) { detail::showName = on; } // escribe el perfil en la linea 0 del master al cambiar
inline void onSelect(std::function<void(const std::string&)> fn) { detail::listener = std::move(fn); } // HookManager: ON PROFILE
// El Replayer la instala: mientras reproduce un .match con el control, los perfiles leen el control grabado
inline void setInputSource(bool (*fn)(int c, int* raw, uint16_t* buttons)) { detail::inputSource = fn; }


/* ---------------------------- CONFIG DEL PERFIL ---------------------------- */

inline void setAxisConfig(Ctrl c, Axis a, AxisConfig cfg) { detail::pad[c].cfg[a] = cfg; }

// Toggles: valores 1..n, empiezan en 1. Se comparten entre perfiles con el mismo id
// (asi un EXTENDS no pierde el estado al cambiar de perfil).
inline void addToggle(const std::string& id, int states) { detail::toggles.try_emplace(id, detail::Toggle{1, states}); }
inline int getToggle(const std::string& id) {
    auto it = detail::toggles.find(id);
    return it == detail::toggles.end() ? 0 : it->second.value;
}
inline void setToggle(const std::string& id, int value) {
    auto it = detail::toggles.find(id);
    if (it != detail::toggles.end()) it->second.value = value;
}
inline void incrementToggle(const std::string& id) {
    auto it = detail::toggles.find(id);
    if (it != detail::toggles.end()) it->second.value = it->second.value % it->second.states + 1;
}

// cooldown(id): true si ya paso el tiempo desde el ultimo true (y reinicia la cuenta)
inline void addCooldown(const std::string& id, uint32_t ms) { detail::cooldowns.try_emplace(id, detail::Cooldown{ms}); }
inline bool cooldown(const std::string& id) {
    auto it = detail::cooldowns.find(id);
    if (it == detail::cooldowns.end()) return true;
    detail::Cooldown& cd = it->second;
    if (cd.used && detail::now - cd.last < cd.ms) return false;
    cd.used = true;
    cd.last = detail::now;
    return true;
}


/* -------------------------------- ENTRADAS -------------------------------- */

inline bool pressing(Ctrl c, Btn b) { return detail::pad[c].cur[b - detail::FIRST]; }
inline bool newPress(Ctrl c, Btn b) { return pressing(c, b) && !detail::pad[c].prev[b - detail::FIRST]; }
inline bool newRelease(Ctrl c, Btn b) { return !pressing(c, b) && detail::pad[c].prev[b - detail::FIRST]; }
// mantenido al menos ms
inline bool held(Ctrl c, Btn b, uint32_t ms) { return pressing(c, b) && detail::now - detail::pad[c].downAt[b - detail::FIRST] >= ms; }

inline int raw(Ctrl c, Axis a) { return detail::pad[c].raw[a]; }

inline double value(Ctrl c, Axis a) {
    detail::Pad& p = detail::pad[c];
    if (p.outTick[a] == detail::tickCount) return p.out[a]; // una vez por tick: el slew no se aplica dos veces
    const AxisConfig& k = p.cfg[a];
    double x = p.raw[a], m = std::fabs(x);
    double y = m <= k.deadzone ? 0 : 127.0 * std::pow((m - k.deadzone) / (127.0 - k.deadzone), k.curve);
    y = std::copysign(y, x) * k.scale * (k.invert ? -1 : 1);
    if (k.slew > 0) y = std::clamp(y, p.out[a] - k.slew, p.out[a] + k.slew);
    p.out[a] = y;
    p.outTick[a] = detail::tickCount;
    return y;
}

inline bool analogNewPress(const std::string& id)   { auto* r = detail::range(id); return r && r->in && !r->prevIn; } // apenas entra
inline bool analogNewRelease(const std::string& id) { auto* r = detail::range(id); return r && !r->in && r->prevIn; } // apenas sale
inline bool analogIn(const std::string& id)         { auto* r = detail::range(id); return r && r->in; }
inline bool analogNotIn(const std::string& id)      { auto* r = detail::range(id); return r && !r->in; }

inline bool doubled(const std::string& id) {
    for (auto& d : detail::doubles)
        if (d.id == id) return d.fired;
    printf("[carbon] double desconocido: %s\n", id.c_str());
    return false;
}

inline bool connected(Ctrl c) { return detail::pad[c].connected; }
inline pros::Controller* controller(Ctrl c) { return detail::ctrl[c]; }
inline pros::Controller* masterController() { return detail::ctrl[0]; }
inline pros::Controller* partnerController() { return detail::ctrl[1]; }


/* -------------------------------- PERFILES -------------------------------- */

// Seguro desde cualquier task: el cambio se aplica al inicio del siguiente tick
inline bool select(const std::string& name) {
    for (int i = 0; i < (int)detail::profiles.size(); i++)
        if (detail::profiles[i].name == name) {
            detail::requested = i;
            return true;
        }
    printf("[carbon] perfil desconocido: %s\n", name.c_str());
    return false;
}
inline void next() { detail::cycle(1); }
inline void previous() { detail::cycle(-1); }
inline std::string currentName() {
    int i = detail::current;
    return i < 0 ? "" : detail::profiles[i].name;
}


/* ---------------------------------- LOOP ---------------------------------- */

inline void tick() {
    using namespace detail;
    now = pros::millis();
    tickCount++;

    int req = requested.exchange(-1);
    if (req >= 0 && req != current) {
        current = req;
        if (showName && ctrl[0]) ctrl[0]->set_text(0, 0, (profiles[req].name + "          ").c_str());
        if (listener) listener(profiles[req].name);
    }

    for (int c = 0; c < 2; c++) {
        Pad& p = pad[c];
        pros::Controller* k = ctrl[c];
        int vraw[4];
        uint16_t vbtn = 0;
        bool virt = inputSource && inputSource(c, vraw, &vbtn); // control reproducido por el Replayer
        p.connected = virt || (k && k->is_connected());
        for (int i = 0; i < BUTTONS; i++) {
            p.prev[i] = p.cur[i];
            p.cur[i] = virt ? (vbtn >> i & 1) : p.connected && k->get_digital(Btn(i + FIRST));
            if (p.cur[i] && !p.prev[i]) p.downAt[i] = now;
        }
        for (int a = 0; a < 4; a++) p.raw[a] = virt ? vraw[a] : p.connected ? k->get_analog(Axis(a)) : 0;
    }

    for (auto& r : ranges) {
        int v = pad[r.c].raw[r.a];
        r.prevIn = r.in;
        r.in = v >= r.lo && v <= r.hi;
    }

    for (auto& d : doubles) {
        bool edge = d.e == DOWN ? newPress(d.c, d.b) : newRelease(d.c, d.b);
        d.fired = false;
        if (!edge) continue;
        if (d.last && now - d.last <= doubleWindow) {
            d.fired = true;
            d.last = 0; // un triple toque no cuenta como dos dobles
        } else {
            d.last = now;
        }
    }

    int i = current;
    if (i >= 0) profiles[i].fn();
}

// Task propio: corre tick() cada periodMs, solo en driver control
inline void start(uint32_t periodMs = 10) {
    if (detail::started) return;
    detail::started = true;
    pros::Task([periodMs] {
        uint32_t t = pros::millis();
        while (true) {
            if (!pros::competition::is_disabled() && !pros::competition::is_autonomous()) tick();
            pros::Task::delay_until(&t, periodMs);
        }
    }, "carbon");
}

} // namespace ProfileManager

namespace Profile = ProfileManager; // el generated, las actions y los hooks usan Profile::
