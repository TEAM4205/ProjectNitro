#pragma once
// ============================================================================
//  replayer.hpp  -  namespace Replayer: reproduce .match y .macro
//
//  Uso basico:
//    Replayer::play("/usd/auton_izq_0001A.macro");   // detecta el tipo por la cabecera
//    Replayer::wait();                                // opcional: bloquea hasta terminar
//
//  .match:  task reader (prio 12, lee el archivo entero antes de arrancar) SD -> anillo de bloques -> task player (prio 13) decodifica,
//           REMUESTREA en su propio reloj con los timestamps reales grabados (interp + EMA)
//           y llama apply() de cada objeto, ligado por NOMBRE (Replayer::bind para cambiarlo).
//           El control grabado queda como control virtual: analog()/digital() y los perfiles
//           de carbon lo leen solos mientras se reproduce.
//  .macro:  se carga entera a RAM (Replayer::load la precarga: arranque instantaneo en autonomo).
//           Hibrido: cada evento sale en su tiempo grabado, pero un evento WAIT_PREV (los de
//           register_action, p. ej. un movimiento PID) espera a que termine la accion
//           interceptada anterior y corre el reloj desde ahi.
// ============================================================================

#include "recorder.hpp"
#include "smoothing.hpp"
#include "nitrogen/heliosmgr/carbon.hpp"
#include <algorithm>
#include <climits>
#include <cmath>

namespace Replayer {

using Interp = rnr::smooth::Interp;
using Args = ActionManager::Args;

struct Config {
    Interp interp = Interp::LINEAR;   // HOLD, LINEAR o CUBIC (Catmull-Rom)
    double ema = 1.0;                 // 1 = sin filtro; 0.3 = bastante suave (agrega atraso)
    double speed = 1.0;               // escala de tiempo: 0.5 = mitad de velocidad
    bool loop = false;
    uint32_t period_ms = 10;          // periodo de apply() / de revision de eventos
    bool external_clock = false;      // true: un .match avanza un paso por cada Replayer::tick() del loop del usuario
    uint32_t ring_bytes = 256 * 1024; // lectura adelantada de .match
    uint32_t wait_timeout_ms = 15000; // tope de espera de WAIT_PREV; despues sigue igual
    uint32_t player_priority = 13;    // se aplican en rnr::init()
    uint32_t reader_priority = 12;    // sobre opcontrol (8): la SD nunca se queda sin turno
    std::function<void()> on_finish;
    std::function<void(const std::string&)> on_error;
};

namespace detail {

struct Group { // canales seguidos de un mismo objeto y perfil
    NORNRDATA* obj;
    uint8_t profile;
    uint16_t first, count;
};

inline Config cfg;
inline std::string path, lastErr, macroPath;
inline std::atomic<bool> playing{false}, stopReq{false}, readerStop{false}, readerDone{true}, initialized{false};
inline std::atomic<uint32_t> posUs{0}, durUs{0};
inline uint64_t stallUs = 0; // tiempo que el player espero al reader (solo lo toca el player)
inline uint32_t dataBytes = 0; // bytes de bloques del .match abierto
inline rnr::fmt::FileHeader hdr{};
inline std::vector<rnr::fmt::TableChannel> table;
inline std::vector<Group> groups;
inline rnr::SlotRing ring;
inline pros::Task* playerTask = nullptr;
inline pros::Task* readerTask = nullptr;
inline std::vector<std::string> macroStrings;
inline std::vector<rnr::codec::Event> macroEvents;

inline void error(const std::string& msg) { rnr::detail::reportError(lastErr, cfg.on_error, msg); }

inline bool readHeader(FILE* f, rnr::fmt::FileHeader& h) {
    return fread(&h, sizeof h, 1, f) == 1 && rnr::fmt::validHeader(h);
}

inline uint8_t fileKind(const std::string& p) {
    if (p == macroPath) return rnr::fmt::MACRO;
    Storage::detail::File f(p, "rb");
    rnr::fmt::FileHeader h;
    return f.ok() && readHeader(f, h) ? h.kind : 0;
}

inline bool loadMacro(const std::string& p) {
    std::vector<uint8_t> raw;
    try {
        raw = Storage::sdbuffer(p);
    } catch (const std::exception& e) {
        error(e.what());
        return false;
    }
    rnr::fmt::FileHeader h;
    if (raw.size() < sizeof h) return error(p + " no es un .macro valido"), false;
    std::memcpy(&h, raw.data(), sizeof h);
    if (!rnr::fmt::validHeader(h) || h.kind != rnr::fmt::MACRO || h.tableBytes > raw.size() - sizeof h)
        return error(p + " no es un .macro valido"), false;
    const uint8_t* tb = raw.data() + sizeof h;
    const uint8_t* ev = tb + h.tableBytes;
    const uint8_t* end = raw.data() + raw.size();
    if (rnr::fmt::crc32(tb, h.tableBytes) != h.tableCrc || !rnr::fmt::decodeStrings(tb, ev, h.tableCount, macroStrings) ||
        !rnr::codec::getEvents(ev, end, macroStrings.size(), macroEvents)) {
        macroPath.clear();
        return error(p + ": macro corrupta"), false;
    }
    macroPath = p;
    return true;
}

// Liga cada grupo de canales del archivo con el objeto del mismo nombre y perfil
inline void buildGroups() {
    groups.clear();
    static ChannelDesc tmp[rnr::codec::MAX_CHANNELS];
    for (size_t i = 0, n = table.size(); i < n;) {
        size_t j = i;
        while (j < n && table[j].object == table[i].object && table[j].profile == table[i].profile) j++;
        Group g{nullptr, 0, uint16_t(i), uint16_t(j - i)};
        auto it = rnr::detail::objects().find(table[i].object);
        if (it == rnr::detail::objects().end()) {
            error("'" + table[i].object + "' no existe: sus canales se ignoran (attach o bind)");
        } else {
            NORNRDATA& d = it->second->rnr();
            int p = rnr::detail::profileIndex(d, table[i].profile);
            if (p < 0 || d.describe(p, tmp, rnr::codec::MAX_CHANNELS) != j - i)
                error("'" + table[i].object + "' ya no tiene el perfil " + table[i].profile + " como se grabo: se ignora");
            else
                g = {&d, uint8_t(p), uint16_t(i), uint16_t(j - i)};
        }
        groups.push_back(g);
        i = j;
    }
}

inline bool openMatch() {
    Storage::detail::File f(path, "rb");
    if (!f.ok()) return error("no se pudo abrir " + path), false;
    if (!readHeader(f, hdr) || hdr.kind != rnr::fmt::MATCH) return error(path + " no es un .match valido"), false;
    std::vector<uint8_t> tbl(hdr.tableBytes);
    if (fread(tbl.data(), 1, tbl.size(), f) != tbl.size() || rnr::fmt::crc32(tbl.data(), tbl.size()) != hdr.tableCrc ||
        hdr.tableCount > rnr::codec::MAX_CHANNELS ||
        !rnr::fmt::decodeMatchTable(tbl.data(), tbl.data() + tbl.size(), hdr.tableCount, table))
        return error(path + ": tabla de canales corrupta"), false;
    long dataStart = ftell(f);
    dataBytes = fseek(f, 0, SEEK_END) == 0 && ftell(f) > dataStart ? uint32_t(ftell(f) - dataStart) : 0;
    buildGroups();
    return true;
}

inline void readBlocks() {
    Storage::detail::File f(path, "rb");
    if (!f.ok() || fseek(f, sizeof(rnr::fmt::FileHeader) + hdr.tableBytes, SEEK_SET) != 0) return;
    while (!readerStop) {
        uint8_t* slot = ring.acquire();
        if (!slot) {
            pros::delay(5);
            continue;
        }
        rnr::fmt::BlockHeader bh;
        if (fread(slot, 1, sizeof bh, f) != sizeof bh) return; // fin del archivo
        std::memcpy(&bh, slot, sizeof bh);
        if (bh.magic != rnr::fmt::BLOCK_MAGIC || sizeof bh + bh.payloadLen > ring.slotSize()) return;
        if (fread(slot + sizeof bh, 1, bh.payloadLen, f) != bh.payloadLen) return; // bloque cortado
        ring.commit();
        pros::delay(1);
    }
}

inline void readerLoop() {
    while (true) {
        pros::Task::notify_take(true, TIMEOUT_MAX);
        readBlocks();
        readerDone = true;
    }
}

// Siguiente frame del archivo; salta bloques corruptos. false = no hay mas.
inline bool nextFrame(rnr::codec::MatchDecoder& dec, bool& open, uint32_t& t, int32_t* v) {
    while (true) {
        if (open) {
            if (dec.next(t, v)) return true;
            ring.pop();
            open = false;
        }
        uint8_t* b = ring.front();
        const uint64_t w0 = pros::micros();
        while (!b) {
            if (stopReq) return false;
            bool done = readerDone;
            if ((b = ring.front())) break;
            if (done) return false;
            pros::delay(1);
        }
        stallUs += pros::micros() - w0; // el reloj del replay no corre mientras la SD no entrega datos
        open = dec.begin(b, ring.slotSize());
        if (!open) ring.pop();
    }
}

inline bool playMatch() {
    if (!openMatch()) return false;
    const uint16_t n = table.size();
    std::vector<uint8_t> every(n);
    std::vector<double> span(n);
    for (uint16_t c = 0; c < n; c++) {
        every[c] = table[c].every;
        span[c] = table[c].flags & rnr::ANGLE ? rnr::smooth::angleSpan(table[c].exp10) : 0;
    }
    static rnr::codec::MatchDecoder dec;
    dec.setup(n, every.data());
    // Anillo del tamano del archivo entero: se lee todo antes de arrancar y la SD no se toca durante el replay.
    // Cada bloque (menos el ultimo) usa al menos slot - peorFrame bytes, asi que esto alcanza para todos.
    const uint32_t slot = std::max<uint32_t>(hdr.blockBytes, 64);
    const uint32_t worst = sizeof(rnr::fmt::BlockHeader) + 5 + (n + 7) / 8 + 5u * n;
    const uint32_t minBlock = slot > worst + 64 ? slot - worst : 64;
    const uint64_t whole = (uint64_t(dataBytes) / minBlock + 2) * slot;
    if (!(whole <= UINT32_MAX && whole > cfg.ring_bytes && ring.allocate(slot, uint32_t(whole))) &&
        !ring.allocate(slot, cfg.ring_bytes)) // sin RAM para todo: streaming con el anillo normal
        return error("sin memoria para el replay"), false;
    readerStop = false;
    readerDone = false;
    readerTask->notify();

    // Ventana de 4 frames p0..p3: el instante actual cae entre p1 y p2
    std::vector<int32_t> win(4 * size_t(n)), out(n);
    std::vector<rnr::smooth::Ema> ema(n);
    uint32_t wt[4] = {};
    int idx[4] = {0, 1, 2, 3}; // slot logico -> fila fisica de win
    auto F = [&](int s) { return win.data() + size_t(idx[s]) * n; };
    bool open = false, eof = false;
    uint32_t lastReal = 0;
    auto pull = [&](int s) { // lee el siguiente frame en el slot s; al final repite el anterior
        if (!eof && nextFrame(dec, open, wt[idx[s]], F(s))) {
            lastReal = wt[idx[s]];
            return;
        }
        eof = true;
        std::memcpy(F(s), F(s - 1), n * sizeof(int32_t));
        wt[idx[s]] = wt[idx[s - 1]];
    };
    auto rotate = [&] {
        int first = idx[0];
        idx[0] = idx[1];
        idx[1] = idx[2];
        idx[2] = idx[3];
        idx[3] = first;
        pull(3);
    };

    // Precarga hasta llenar el anillo (normalmente = el archivo entero) antes de arrancar el reloj
    while (!stopReq && !readerDone && ring.used() < ring.slots()) pros::delay(1);

    if (nextFrame(dec, open, wt[idx[1]], F(1))) {
        lastReal = wt[idx[1]];
        std::memcpy(F(0), F(1), n * sizeof(int32_t));
        wt[idx[0]] = wt[idx[1]];
        pull(2);
        pull(3);

        durUs = hdr.flags & rnr::fmt::CLEAN ? hdr.durationUs : 0;
        const uint32_t firstT = wt[idx[1]];
        // HOLD toma el frame mas CERCANO en el tiempo (no el ultimo pasado): con el mismo periodo que la
        // grabacion, el frame k sale en la iteracion k aunque el reloj tiemble unos microsegundos.
        const double bias = cfg.interp == Interp::HOLD ? hdr.periodUs / 2.0 : 0.0;
        if (cfg.external_clock) { // los ticks que llegaron durante la precarga no cuentan: el reloj arranca en el primero real
            pros::Task::current().notify_clear();
            rnr::detail::waitTick(cfg.period_ms);
        }
        const uint64_t start = pros::micros();
        stallUs = 0;
        uint32_t tick = pros::millis();
        while (!stopReq) {
            double T = firstT + double(pros::micros() - start - stallUs) * cfg.speed;
            while (T + bias >= wt[idx[2]] && !(eof && wt[idx[2]] >= lastReal)) rotate();
            if (eof && T >= lastReal) break;

            double t1 = wt[idx[1]], t2 = wt[idx[2]];
            double u = t2 > t1 ? std::clamp((T - t1) / (t2 - t1), 0.0, 1.0) : 0.0;
            const int32_t *p0 = F(0), *p1 = F(1), *p2 = F(2), *p3 = F(3);
            for (uint16_t c = 0; c < n; c++) {
                uint8_t fl = table[c].flags;
                double val = rnr::smooth::interpolate(cfg.interp, fl, span[c], p0[c], p1[c], p2[c], p3[c], u);
                if (!(fl & (rnr::DIGITAL | rnr::ANGLE))) val = ema[c].step(val, cfg.ema);
                out[c] = int32_t(std::clamp(std::llround(val), (long long)INT32_MIN, (long long)INT32_MAX));
            }
            for (auto& g : groups)
                if (g.obj) g.obj->apply(g.profile, out.data() + g.first);
            posUs = uint32_t(T - firstT);
            if (cfg.external_clock) rnr::detail::waitTick(cfg.period_ms);
            else pros::Task::delay_until(&tick, cfg.period_ms);
        }
    }

    for (auto& g : groups)
        if (g.obj) g.obj->rest(g.profile);
    readerStop = true;
    while (!readerDone) pros::delay(1);
    ring.release();
    return true;
}

inline bool playMacro() {
    if (path != macroPath && !loadMacro(path)) return false;
    const auto& events = macroEvents;
    durUs = events.empty() ? 0 : events.back().tUs;
    std::map<std::string, Args> active; // PRESSING sostenidas: se ejecutan cada periodo
    std::string prevIntercepted;
    const uint64_t start = pros::micros();
    double offset = 0; // corrimiento acumulado por las esperas WAIT_PREV
    size_t i = 0;
    bool waiting = false;
    uint32_t waitStart = 0, tick = pros::millis();

    while (!stopReq) {
        double now = double(pros::micros() - start) * cfg.speed - offset;
        while (i < events.size() && events[i].tUs <= now) {
            const auto& e = events[i];
            const std::string& name = macroStrings[e.action];
            if ((e.flags & rnr::codec::WAIT_PREV) && !prevIntercepted.empty() && !ActionManager::isFinished(prevIntercepted)) {
                if (!waiting) {
                    waiting = true;
                    waitStart = pros::millis();
                }
                if (pros::millis() - waitStart < cfg.wait_timeout_ms) {
                    offset = double(pros::micros() - start) * cfg.speed - e.tUs; // el reloj se queda en este evento
                    break;
                }
                error("timeout esperando a " + prevIntercepted + ": sigo igual");
            }
            waiting = false;
            if (e.phase == ActionManager::PRESSING) active[name] = e.args;
            else if (e.phase == ActionManager::UP && active.erase(name)) {} // fin de un PRESSING
            else ActionManager::execute(name, macroStrings[e.state], e.args);
            if (e.flags & rnr::codec::INTERCEPTED) prevIntercepted = name;
            i++;
        }
        for (auto& [name, args] : active) ActionManager::execute(name, "", args);
        posUs = uint32_t(std::max(now, 0.0));
        if (i >= events.size()) break;
        pros::Task::delay_until(&tick, cfg.period_ms);
    }
    return true;
}

inline void playerLoop() {
    while (true) {
        pros::Task::notify_take(true, TIMEOUT_MAX);
        do {
            uint8_t kind = fileKind(path);
            bool ok = kind == rnr::fmt::MATCH ? playMatch() : kind == rnr::fmt::MACRO ? playMacro() : false;
            if (!kind) error("no se pudo leer " + path + " (no existe o no es .match/.macro)");
            if (!ok) break; // un archivo malo no se repite en loop
        } while (cfg.loop && !stopReq);
        pros::Task::current().notify_clear(); // ticks sobrantes no deben arrancar otra reproduccion
        playing = false;
        if (cfg.on_finish) cfg.on_finish();
    }
}

// Para carbon: mientras se reproduce el control, los perfiles leen el control grabado
inline bool padSource(int c, int* raw, uint16_t* buttons) {
    auto& p = rnr::detail::pads[c];
    if (!p.active) return false;
    for (int a = 0; a < 4; a++) raw[a] = p.ax[a];
    *buttons = p.btn;
    return true;
}

inline void init() {
    if (initialized.exchange(true)) return;
    ProfileManager::setInputSource(&padSource);
    playerTask = new pros::Task(playerLoop, cfg.player_priority, 0x800, "rnr_player");
    readerTask = new pros::Task(readerLoop, cfg.reader_priority, 0x1000, "rnr_reader");
}

} // namespace detail


/* --------------------------------------- API --------------------------------------- */

inline Config& config() { return detail::cfg; }
inline std::string last_error() { return detail::lastErr; }
inline bool is_playing() { return detail::playing; }

// Con external_clock: llamar una vez por iteracion del loop, ANTES de leer el control reproducido.
// El player (prioridad mayor) aplica el frame de ese instante y devuelve el turno.
inline void tick() {
    if (detail::cfg.external_clock && detail::playing) detail::playerTask->notify();
}

// Empieza a reproducir en segundo plano. false si no se pudo ni empezar; los errores de
// lectura llegan despues por on_error / last_error().
inline bool play(const std::string& file) {
    using namespace detail;
    if (!initialized) return error("falta rnr::init() en initialize()"), false;
    if (file.empty()) return error("no hay archivo para reproducir"), false;
    if (playing.exchange(true)) return error("ya se esta reproduciendo"), false;
    path = file;
    stopReq = false;
    posUs = 0;
    durUs = 0;
    playerTask->notify();
    return true;
}

// Precarga una .macro en RAM (en initialize o al inicio del autonomo): play() arranca sin tocar la SD
inline bool load(const std::string& file) {
    if (detail::playing) return detail::error("no se puede cargar mientras se reproduce"), false;
    return detail::loadMacro(file);
}

inline void stop() {
    if (detail::playing) detail::stopReq = true;
}

inline void wait() {
    while (detail::playing) pros::delay(5);
}

// 0..1 (0 si el archivo no se cerro limpio y no se sabe su duracion)
inline double progress() {
    uint32_t d = detail::durUs;
    return d ? std::min(1.0, double(detail::posUs) / d) : 0.0;
}

// Liga los canales grabados con el nombre `name` a este objeto (por defecto se usa el attacheado con ese nombre)
inline void bind(const std::string& name, Neobject& obj) { rnr::detail::objects()[name] = &obj; }

// Control reproducido (de un .match con el control attacheado)
inline bool controller_active(pros::controller_id_e_t c = pros::E_CONTROLLER_MASTER) { return rnr::detail::pads[c].active; }
inline int analog(pros::controller_analog_e_t axis, pros::controller_id_e_t c = pros::E_CONTROLLER_MASTER) {
    return rnr::detail::pads[c].ax[axis];
}
inline bool digital(pros::controller_digital_e_t b, pros::controller_id_e_t c = pros::E_CONTROLLER_MASTER) {
    return rnr::detail::pads[c].btn >> (b - pros::E_CONTROLLER_DIGITAL_L1) & 1;
}

} // namespace Replayer
