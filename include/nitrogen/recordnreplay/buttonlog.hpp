#pragma once
// ============================================================================
//  buttonlog.hpp  -  ButtonLog: graba SOLO cuando se presiona y suelta cada boton del control
//  master, en texto legible (ms desde el inicio, con pros::millis):
//
//    500 DOWN A
//    1000 UP A
//    1830 END
//
//  Grabando, las lineas van a RAM y el archivo se escribe a la SD al parar (nada de SD mientras
//  manejas). Reproduciendo, los botones van al control virtual del RnR: Replayer::digital() y los
//  perfiles de carbon los leen solos (controller_active() es true; los sticks quedan en 0).
//
//  Uso, en el loop del driver con periodo fijo (delay_until):
//    ButtonLog::tick();                       // una vez por iteracion, ANTES de leer el control
//    ButtonLog::start("intake") / stop()      // graba -> /usd/intake_XXXXX.btn
//    ButtonLog::play(ButtonLog::last_file())  // reproduce; para solo al llegar a END
//
//  Exactitud: el tiempo 0 se toma en el primer tick() despues de start()/play(), asi grabacion y
//  replay miden en el mismo punto del loop. Con un loop de 10 ms, cada evento sale en la misma
//  iteracion relativa en que se grabo.
// ============================================================================

#include "replayer.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ButtonLog {

namespace detail {

// Orden de PROS desde E_CONTROLLER_DIGITAL_L1: el bit i del control virtual es el boton L1 + i
inline const char* const names[12] = {"L1", "L2", "R1", "R2", "UP", "DOWN", "LEFT", "RIGHT", "X", "B", "Y", "A"};

struct Event {
    uint32_t t;
    uint8_t button;
    bool down;
};

enum Mode : uint8_t { IDLE, RECORDING, PLAYING };

inline Mode mode = IDLE;
inline bool armed = false; // el reloj arranca en el proximo tick()
inline uint32_t t0 = 0, endT = 0;
inline uint16_t prev = 0;
inline std::string text, path, lastPath, lastErr;
inline std::vector<Event> events;
inline size_t next = 0;

inline bool error(const std::string& msg) {
    lastErr = msg;
    printf("[buttonlog] %s\n", msg.c_str());
    return false;
}

inline uint16_t readButtons() {
    static pros::Controller master(pros::E_CONTROLLER_MASTER);
    uint16_t b = 0;
    for (int i = 0; i < 12; i++)
        b |= uint16_t(master.get_digital(pros::controller_digital_e_t(pros::E_CONTROLLER_DIGITAL_L1 + i)) != 0) << i;
    return b;
}

inline void releasePad() {
    auto& p = rnr::detail::pads[pros::E_CONTROLLER_MASTER];
    p.active = false;
    p.btn = 0;
    for (auto& a : p.ax) a = 0;
}

// Una linea "t DOWN|UP BOTON" o "t END". false si la linea no se entiende.
inline bool parseLine(const char* line) {
    unsigned long t;
    char a[8] = {}, b[8] = {};
    int k = sscanf(line, "%lu %7s %7s", &t, a, b);
    if (k == 2 && !strcmp(a, "END")) {
        endT = uint32_t(t);
        return true;
    }
    if (k != 3 || (strcmp(a, "DOWN") && strcmp(a, "UP"))) return false;
    for (uint8_t i = 0; i < 12; i++)
        if (!strcmp(b, names[i])) {
            events.push_back({uint32_t(t), i, a[0] == 'D'});
            return true;
        }
    return false;
}

} // namespace detail


inline bool is_recording() { return detail::mode == detail::RECORDING; }
inline bool is_playing() { return detail::mode == detail::PLAYING; }
inline bool busy() { return detail::mode != detail::IDLE; }
inline std::string last_file() { return detail::lastPath; }
inline std::string last_error() { return detail::lastErr; }

inline bool start(const std::string& name = "buttons", const std::string& dir = "/usd/") {
    using namespace detail;
    if (mode != IDLE || Recorder::is_busy() || Replayer::is_playing()) return error("ya hay algo grabando o reproduciendo");
    if (!Storage::sdcardok()) return error("no hay tarjeta SD");
    path = rnr::detail::makePath(dir, name, ".btn");
    text.clear();
    text.reserve(4096);
    armed = true;
    mode = RECORDING;
    return true;
}

// Grabando: escribe el archivo (unos pocos ms). Reproduciendo: suelta todos los botones.
inline void stop() {
    using namespace detail;
    if (mode == RECORDING) {
        char line[24];
        snprintf(line, sizeof line, "%lu END\n", armed ? 0ul : (unsigned long)(pros::millis() - t0));
        text += line;
        Storage::detail::File f(path, "w");
        if (!f.ok()) error("no se pudo crear " + path);
        else if (fwrite(text.data(), 1, text.size(), f) != text.size()) error("error escribiendo " + path);
        else lastPath = path;
    } else if (mode == PLAYING) {
        releasePad();
    }
    mode = IDLE;
}

inline bool play(const std::string& file) {
    using namespace detail;
    if (mode != IDLE || Recorder::is_busy() || Replayer::is_playing()) return error("ya hay algo grabando o reproduciendo");
    if (file.empty()) return error("no hay archivo para reproducir");
    std::vector<uint8_t> raw;
    try {
        raw = Storage::sdbuffer(file);
    } catch (const std::exception& e) {
        return error(e.what());
    }
    events.clear();
    endT = 0;
    std::string s(raw.begin(), raw.end());
    for (size_t i = 0; i < s.size();) {
        size_t j = s.find('\n', i);
        if (j == std::string::npos) j = s.size();
        std::string line = s.substr(i, j - i);
        i = j + 1;
        if (line.find_first_not_of(" \r\t") != std::string::npos && !parseLine(line.c_str()))
            return error(file + ": linea invalida '" + line + "'");
    }
    for (auto& e : events) // sin END (archivo cortado): termina en el ultimo evento
        if (e.t > endT) endT = e.t;
    releasePad();
    rnr::detail::pads[pros::E_CONTROLLER_MASTER].active = true;
    next = 0;
    armed = true;
    mode = PLAYING;
    return true;
}

// Una vez por iteracion del loop, antes de leer el control.
inline void tick() {
    using namespace detail;
    if (mode == IDLE) return;
    uint32_t now = pros::millis();
    if (armed) {
        armed = false;
        t0 = now;
        if (mode == RECORDING) prev = readButtons(); // botones ya apretados al empezar no cuentan
    }
    uint32_t t = now - t0;
    if (mode == RECORDING) {
        uint16_t b = readButtons(), changed = b ^ prev;
        prev = b;
        for (int i = 0; i < 12; i++)
            if (changed >> i & 1) {
                char line[32];
                snprintf(line, sizeof line, "%lu %s %s\n", (unsigned long)t, b >> i & 1 ? "DOWN" : "UP", names[i]);
                text += line;
            }
        return;
    }
    auto& pad = rnr::detail::pads[pros::E_CONTROLLER_MASTER];
    uint16_t btn = pad.btn;
    for (; next < events.size() && events[next].t <= t; next++)
        btn = events[next].down ? btn | (1u << events[next].button) : btn & ~(1u << events[next].button);
    pad.btn = btn;
    if (t >= endT) stop();
}

} // namespace ButtonLog
