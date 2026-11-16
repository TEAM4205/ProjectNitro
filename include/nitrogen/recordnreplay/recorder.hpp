#pragma once
// ============================================================================
//  recorder.hpp  -  namespace Recorder: graba .match (modo activo) y .macro (modo pasivo)
//
//  Uso basico:
//    rnr::init();                                  // en initialize(), una sola vez
//    Recorder::attach_controller();                // canal del control master
//    Recorder::attach(brazo, "ACTUAL");            // cualquier Neobject con su perfil
//    Recorder::start_match("final");  ...  Recorder::stop();
//    Recorder::start_macro("auton_izq");  ...  Recorder::stop();
//
//  Modo activo (.match):
//    task sampler (prio 13) --cada period_ms--> sample() de cada objeto -> codifica el frame
//    DIRECTO en un bloque del anillo -> task writer (prio 12) -> un fwrite por bloque -> SD
//    La SD no se toca mientras se graba: el writer espera a que pare (o a que el anillo pase
//    de la mitad) y recien ahi crea el archivo y lo escribe.
//    Si el anillo se llena, los frames se cuentan como perdidos y el timestamp del siguiente
//    bloque deja el hueco marcado (el replay interpola por encima).
//
//  Modo pasivo (.macro):
//    ActionManager::trigger avisa cada accion del driver; las intercepciones RECORD_*
//    llaman a Recorder::register_action. Los eventos van a RAM (son pocos bytes) y el
//    writer guarda el archivo entero al parar.
//
//  Las tasks se crean en rnr::init() y viven siempre: las tasks de competencia mueren al
//  cambiar de autonomo a driver, y eso no puede cortar una escritura a medias.
// ============================================================================

#include "api.h"
#include "codec.hpp"
#include "ring.hpp"
#include "nitrogen/Storage.hpp"
#include "nitrogen/heliosmgr/iron.hpp"
#include "nitrogen/neobjects/neobject.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/* ------------------------- compartido entre Recorder y Replayer ------------------------- */
namespace rnr::detail {

// Objetos por nombre: Recorder::attach y Replayer::bind lo llenan; el Replayer liga por nombre
inline std::map<std::string, Neobject*>& objects() {
    static std::map<std::string, Neobject*> m;
    return m;
}

// Control reproducido: lo escribe ControllerObject::apply y lo leen Replayer::analog/digital y carbon
struct VirtualPad {
    std::atomic<bool> active{false};
    std::atomic<int32_t> ax[4]{};
    std::atomic<uint16_t> btn{0};
};
inline VirtualPad pads[2];

inline int profileIndex(NORNRDATA& d, const std::string& name) {
    for (uint8_t i = 0; i < d.profile_count(); i++)
        if (name == d.profile_name(i)) return i;
    return -1;
}

// El control como Neobject: perfil "ALL" = lx, ly, rx, ry y la mascara de 12 botones (L1..A)
class ControllerObject : public Neobject, public NORNRDATA {
public:
    explicit ControllerObject(pros::controller_id_e_t id) : id(id), ctrl(id) {}
    const char* name() const override { return id == pros::E_CONTROLLER_MASTER ? "master" : "partner"; }
    NORNRDATA& rnr() override { return *this; }

    uint8_t profile_count() const override { return 1; }
    const char* profile_name(uint8_t) const override { return "ALL"; }
    uint8_t describe(uint8_t, ChannelDesc* out, uint8_t max) const override {
        static const char* names[5] = {"lx", "ly", "rx", "ry", "buttons"};
        uint8_t n = max < 5 ? max : 5;
        for (uint8_t i = 0; i < n; i++) out[i] = {names[i], 0, 1, uint8_t(APPLYABLE | (i == 4 ? DIGITAL : 0))};
        return n;
    }
    // Deja la muestra tambien en el pad virtual: el loop del usuario lee (Replayer::analog/digital)
    // exactamente lo que quedo grabado, no una segunda lectura del control que puede diferir.
    void sample(uint8_t, int32_t* out) override {
        VirtualPad& p = pads[id];
        for (int a = 0; a < 4; a++) p.ax[a] = out[a] = ctrl.get_analog(pros::controller_analog_e_t(a));
        uint32_t b = 0;
        for (int i = 0; i < 12; i++)
            b |= uint32_t(ctrl.get_digital(pros::controller_digital_e_t(pros::E_CONTROLLER_DIGITAL_L1 + i)) != 0) << i;
        p.btn = uint16_t(out[4] = int32_t(b));
    }
    void apply(uint8_t, const int32_t* in) override {
        VirtualPad& p = pads[id];
        for (int a = 0; a < 4; a++) p.ax[a] = in[a];
        p.btn = uint16_t(in[4]);
        p.active = true;
    }
    void rest(uint8_t) override {
        VirtualPad& p = pads[id];
        for (int a = 0; a < 4; a++) p.ax[a] = 0;
        p.btn = 0;
        p.active = false;
    }

private:
    pros::controller_id_e_t id;
    pros::Controller ctrl;
};
inline ControllerObject* controllers[2] = {nullptr, nullptr};

// Reloj externo: el loop del usuario marca cada frame con Recorder::tick() / Replayer::tick(), asi el
// muestreo y la lectura del control caen en la misma iteracion y el replay cae en la misma iteracion.
// Si el loop deja de llamar (cambio de modo), la task sigue sola cada 5 periodos para poder terminar.
inline void waitTick(uint32_t periodMs) { pros::Task::notify_take(true, periodMs * 5); }

inline void reportError(std::string& slot, const std::function<void(const std::string&)>& cb, const std::string& msg) {
    slot = msg;
    printf("[rnr] %s\n", msg.c_str());
    if (cb) cb(msg);
}

// directorio/nombre_RUNID.ext  (run id persistente en la SD: nunca se pisa una grabacion)
inline std::string makePath(const std::string& dir, const std::string& name, const char* ext) {
    uint32_t id;
    try {
        id = Storage::sdnextrunid();
    } catch (...) {
        id = pros::millis();
    }
    char buf[16];
    snprintf(buf, sizeof buf, "_%05lX", (unsigned long)(id & 0xFFFFF));
    std::string d = dir;
    if (!d.empty() && d.back() != '/') d += '/';
    return d + name + buf + ext;
}

} // namespace rnr::detail


namespace Recorder {

using Args = ActionManager::Args;

struct Config {
    std::string directory = "/usd/";        // VEX recomienda la raiz de la SD
    uint32_t period_ms = 10;                // 10 ms = 100 Hz: lo maximo util, VEXos publica datos cada 10 ms
    uint32_t ring_bytes = 512 * 1024;       // colchon en RAM contra picos lentos de la SD (~200 ms)
    uint32_t block_bytes = 4096;            // un fwrite por bloque (8 sectores); si no cabe un frame se agranda solo
    bool external_clock = false;            // true: cada frame se muestrea cuando el loop del usuario llama Recorder::tick()
    bool auto_match = false;                // empieza solo al entrar a autonomo y para al deshabilitar tras el driver
    std::string auto_match_name = "match";
    uint32_t sampler_priority = 13;         // bajo el daemon de PROS (14). Se aplican en rnr::init()
    uint32_t writer_priority = 12;          // sobre opcontrol (8): si el writer no tiene turno el anillo se llena y se pierden frames
    std::function<void(const std::string&)> on_error;
};

struct Stats {
    uint32_t frames = 0, dropped_frames = 0, blocks = 0, events = 0;
    uint32_t late_frames = 0;                // frames que llegaron > 1.5 periodos tarde (el sampler se trabo)
    uint32_t bytes = 0;
    uint32_t max_write_ms = 0;               // peor fwrite de un bloque: la latencia real de tu SD
    uint32_t min_dt_us = 0, max_dt_us = 0;   // periodo real entre frames
    uint32_t duration_ms = 0;
};

bool start_match(const std::string& name = "");
void stop();

namespace detail {

enum Mode : uint8_t { IDLE, MATCH, MACRO };

struct Attached {
    Neobject* obj;
    uint8_t profile;
    uint16_t first, count; // posicion de sus canales en el frame
};

inline Config cfg;
inline Stats st;
inline std::atomic<uint8_t> mode{IDLE};
inline std::atomic<bool> stopReq{false}, samplerDone{false}, initialized{false};
inline std::atomic<uint32_t> lastUs{0};
inline std::vector<Attached> attached;
inline std::vector<rnr::fmt::TableChannel> table;
inline std::vector<uint8_t> every;
inline rnr::SlotRing ring;
inline std::string curPath, lastPath, lastErr;
inline pros::Task* samplerTask = nullptr;
inline pros::Task* writerTask = nullptr;
inline uint64_t startUs = 0;
inline bool autoActive = false, sawDriver = false;

// .macro
inline pros::Mutex* macroMutex = nullptr;
inline std::vector<uint8_t> macroData;
inline std::vector<std::string> macroStrings;
inline uint32_t macroLastT = 0;
inline std::map<std::string, Args> pressing; // PRESSING activas: solo se graban cuando cambian sus args

inline void error(const std::string& msg) { rnr::detail::reportError(lastErr, cfg.on_error, msg); }

inline uint32_t intern(const std::string& s) {
    for (uint32_t i = 0; i < macroStrings.size(); i++)
        if (macroStrings[i] == s) return i;
    macroStrings.push_back(s);
    return macroStrings.size() - 1;
}

// Con macroMutex tomado
inline void pushEvent(const std::string& action, const std::string& state, uint8_t phase, uint8_t flags, const Args& args) {
    rnr::codec::Event e;
    e.tUs = uint32_t(pros::micros() - startUs);
    e.action = intern(action);
    e.state = intern(state);
    e.phase = phase;
    e.flags = flags;
    e.args = args;
    rnr::codec::putEvent(macroData, e, macroLastT);
    st.events++;
}

// Sink de ActionManager::trigger (acciones sin intercepciones)
inline void onAction(const std::string& name, ActionManager::Phase phase, const Args& args, bool fires) {
    if (mode != MACRO || stopReq) return;
    std::lock_guard<pros::Mutex> lock(*macroMutex);
    if (phase == ActionManager::PRESSING) {
        if (!fires) return;
        auto it = pressing.find(name);
        if (it != pressing.end() && it->second == args) return;
        pressing[name] = args;
        pushEvent(name, "", phase, 0, args);
    } else if (phase == ActionManager::UP && pressing.erase(name)) {
        pushEvent(name, "", phase, 0, {}); // fin de un PRESSING
    } else if (fires) {
        pushEvent(name, "", phase, 0, args);
    }
}

inline bool buildTable() {
    table.clear();
    every.clear();
    static ChannelDesc buf[rnr::codec::MAX_CHANNELS];
    for (auto& a : attached) {
        NORNRDATA& d = a.obj->rnr();
        uint8_t n = d.describe(a.profile, buf, rnr::codec::MAX_CHANNELS);
        if (table.size() + n > rnr::codec::MAX_CHANNELS) {
            error("demasiados canales (maximo 255): usa perfiles mas chicos");
            return false;
        }
        a.first = table.size();
        a.count = n;
        for (uint8_t i = 0; i < n; i++) {
            uint8_t e = buf[i].every ? buf[i].every : 1;
            table.push_back({a.obj->name(), d.profile_name(a.profile), buf[i].name ? buf[i].name : "", buf[i].exp10, e, buf[i].flags});
            every.push_back(e);
        }
    }
    if (table.empty()) error("los objetos attacheados no tienen canales");
    return !table.empty();
}

inline void autoMatchTick() {
    if (!cfg.auto_match) return;
    bool disabled = pros::competition::is_disabled(), auton = pros::competition::is_autonomous();
    if (!autoActive) {
        if (mode == IDLE && !disabled && auton && start_match(cfg.auto_match_name)) {
            autoActive = true;
            sawDriver = false;
        }
        return;
    }
    if (!disabled && !auton) sawDriver = true;
    if (disabled && sawDriver) { // termino el driver: fin del partido
        stop();
        autoActive = false;
    }
}

inline void recordMatch() {
    const uint16_t n = table.size();
    std::vector<int32_t> v(n);
    static rnr::codec::MatchEncoder enc;
    enc.setup(n, every.data());
    uint8_t* slot = nullptr;
    uint32_t seq = 0, k = 0, last = 0;
    if (cfg.external_clock) rnr::detail::waitTick(cfg.period_ms); // el frame 0 cae en el primer tick del loop
    startUs = pros::micros();
    uint32_t tick = pros::millis();
    while (!stopReq) {
        autoMatchTick();
        uint32_t t = uint32_t(pros::micros() - startUs);
        for (auto& a : attached) a.obj->rnr().sample(a.profile, v.data() + a.first);
        if (k) {
            uint32_t dt = t - last;
            if (!st.min_dt_us || dt < st.min_dt_us) st.min_dt_us = dt;
            if (dt > st.max_dt_us) st.max_dt_us = dt;
            if (dt > cfg.period_ms * 1500) st.late_frames++;
        }
        last = t;

        if (!slot && (slot = ring.acquire())) enc.begin(slot, ring.slotSize(), seq, k, t);
        if (slot && !enc.add(t, k, v.data())) { // bloque lleno: al anillo y otro nuevo
            enc.finish();
            ring.commit();
            seq++;
            st.blocks++;
            if ((slot = ring.acquire())) {
                enc.begin(slot, ring.slotSize(), seq, k, t);
                enc.add(t, k, v.data());
            }
        }
        if (slot) st.frames++;
        else st.dropped_frames++;
        k++;
        if (cfg.external_clock) rnr::detail::waitTick(cfg.period_ms);
        else pros::Task::delay_until(&tick, cfg.period_ms);
    }
    if (slot && !enc.empty()) {
        enc.finish();
        ring.commit();
        st.blocks++;
    }
    lastUs = last;
    st.duration_ms = last / 1000;
    samplerDone = true;
    writerTask->notify();
}

inline void samplerLoop() {
    while (true) {
        pros::Task::notify_take(true, 20); // 20 ms: tambien vigila auto_match
        autoMatchTick();
        if (mode == MATCH && !samplerDone) recordMatch();
    }
}

inline rnr::fmt::FileHeader makeHeader(uint8_t kind, uint32_t blockBytes, const std::vector<uint8_t>& tbl, uint32_t count) {
    rnr::fmt::FileHeader h{};
    h.magic = rnr::fmt::FILE_MAGIC;
    h.version = rnr::fmt::VERSION;
    h.kind = kind;
    h.periodUs = cfg.period_ms * 1000;
    h.blockBytes = blockBytes;
    h.tableBytes = tbl.size();
    h.tableCount = count;
    h.tableCrc = rnr::fmt::crc32(tbl.data(), tbl.size());
    rnr::fmt::sealHeader(h);
    return h;
}

inline void writeMatch() {
    // La SD no se toca mientras se graba (crear el archivo y cada fwrite traban el loop del driver):
    // todo queda en el anillo y se guarda al parar.
    // shortcut: si el anillo pasa de la mitad empieza a escribir igual (vuelven las trabas), subir ring_bytes si pasa
    while (!samplerDone && ring.used() < ring.slots() / 2) pros::delay(5);
    bool ok = true;
    {
        Storage::detail::File f(curPath, "wb");
        std::vector<uint8_t> tbl = rnr::fmt::encodeMatchTable(table);
        rnr::fmt::FileHeader h = makeHeader(rnr::fmt::MATCH, ring.slotSize(), tbl, table.size());
        if (!f.ok()) {
            error("no se pudo crear " + curPath);
            ok = false;
            stopReq = true;
        } else if (fwrite(&h, sizeof h, 1, f) != 1 || fwrite(tbl.data(), 1, tbl.size(), f) != tbl.size()) {
            error("error escribiendo la cabecera de " + curPath);
            ok = false;
            stopReq = true;
        }
        st.bytes = sizeof h + tbl.size();

        while (true) {
            bool done = samplerDone; // leer ANTES de vaciar: todo lo que entro antes se escribe
            while (uint8_t* b = ring.front()) {
                if (ok) {
                    uint32_t len = rnr::codec::MatchDecoder::blockSize(b), t0 = pros::millis();
                    if (fwrite(b, 1, len, f) != len) {
                        error("error escribiendo en la SD (llena o retirada?)");
                        ok = false;
                        stopReq = true;
                    } else {
                        st.bytes += len;
                        uint32_t ms = pros::millis() - t0;
                        if (ms > st.max_write_ms) st.max_write_ms = ms;
                    }
                }
                ring.pop();
                pros::delay(1); // cede la CPU entre bloques
            }
            if (done) break;
            pros::delay(5);
        }

        if (ok) { // cierre limpio: la cabecera pasa a tener frames y duracion
            h.flags = rnr::fmt::CLEAN;
            h.frames = st.frames;
            h.durationUs = lastUs;
            rnr::fmt::sealHeader(h);
            if (fseek(f, 0, SEEK_SET) == 0) fwrite(&h, sizeof h, 1, f);
        }
    } // fclose
    ring.release();
    if (ok) lastPath = curPath;
    mode = IDLE;
}

inline void writeMacro() {
    std::vector<uint8_t> data;
    std::vector<std::string> strings;
    uint32_t dur;
    {
        std::lock_guard<pros::Mutex> lock(*macroMutex);
        data.swap(macroData);
        strings.swap(macroStrings);
        dur = macroLastT;
        pressing.clear();
    }
    std::vector<uint8_t> tbl = rnr::fmt::encodeStrings(strings);
    rnr::fmt::FileHeader h = makeHeader(rnr::fmt::MACRO, 0, tbl, strings.size());
    h.flags = rnr::fmt::CLEAN;
    h.frames = st.events;
    h.durationUs = dur;
    rnr::fmt::sealHeader(h);

    bool ok = false;
    {
        Storage::detail::File f(curPath, "wb");
        if (!f.ok()) {
            error("no se pudo crear " + curPath);
        } else if (fwrite(&h, sizeof h, 1, f) == 1 && fwrite(tbl.data(), 1, tbl.size(), f) == tbl.size()) {
            ok = true;
            for (size_t off = 0; ok && off < data.size(); off += 4096) { // en trozos, cediendo la CPU
                size_t len = data.size() - off < 4096 ? data.size() - off : 4096;
                ok = fwrite(data.data() + off, 1, len, f) == len;
                pros::delay(1);
            }
            if (!ok) error("error escribiendo " + curPath);
        }
    }
    st.bytes = sizeof h + tbl.size() + data.size();
    st.duration_ms = dur / 1000;
    if (ok) lastPath = curPath;
    mode = IDLE;
}

inline void writerLoop() {
    while (true) {
        pros::Task::notify_take(true, TIMEOUT_MAX);
        if (mode == MATCH) writeMatch();
        else if (mode == MACRO && stopReq) writeMacro();
    }
}

inline void init() {
    if (initialized.exchange(true)) return;
    macroMutex = new pros::Mutex();
    for (int c = 0; c < 2; c++) {
        auto* obj = new rnr::detail::ControllerObject(pros::controller_id_e_t(c));
        rnr::detail::controllers[c] = obj;
        rnr::detail::objects()[obj->name()] = obj;
    }
    ActionManager::setRecordSink(&onAction);
    samplerTask = new pros::Task(samplerLoop, cfg.sampler_priority, 0x800, "rnr_sampler");
    writerTask = new pros::Task(writerLoop, cfg.writer_priority, 0x1000, "rnr_writer");
}

} // namespace detail


/* --------------------------------------- API --------------------------------------- */

inline Config& config() { return detail::cfg; }
inline Stats stats() { return detail::st; }
inline std::string last_error() { return detail::lastErr; }
inline std::string last_file() { return detail::lastPath; } // ultimo archivo guardado completo
inline bool is_recording() { return detail::mode != detail::IDLE && !detail::stopReq; }
inline bool is_busy() { return detail::mode != detail::IDLE; } // grabando o todavia guardando

// Con external_clock: llamar una vez por iteracion del loop, ANTES de leer el control. El sampler
// (prioridad mayor) muestrea el frame en ese instante y devuelve el turno.
inline void tick() {
    using namespace detail;
    if (cfg.external_clock && mode == MATCH && !samplerDone) samplerTask->notify();
}

// Agrega un objeto a la grabacion activa con uno de sus perfiles. No durante una grabacion.
inline bool attach(Neobject& obj, const std::string& profile = "ALL") {
    using namespace detail;
    if (is_busy()) {
        error("no se puede attachear mientras se graba");
        return false;
    }
    int p = rnr::detail::profileIndex(obj.rnr(), profile);
    if (p < 0) {
        error(std::string(obj.name()) + " no tiene el perfil " + profile);
        return false;
    }
    rnr::detail::objects()[obj.name()] = &obj;
    for (auto& a : attached)
        if (a.obj == &obj) {
            a.profile = p;
            return true;
        }
    attached.push_back({&obj, uint8_t(p), 0, 0});
    return true;
}

inline void detach(Neobject& obj) {
    if (is_busy()) return;
    auto& v = detail::attached;
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->obj == &obj) {
            v.erase(it);
            return;
        }
}

inline bool attach_controller(pros::controller_id_e_t id = pros::E_CONTROLLER_MASTER) {
    if (!rnr::detail::controllers[id]) {
        detail::error("falta rnr::init() en initialize()");
        return false;
    }
    return attach(*rnr::detail::controllers[id], "ALL");
}

inline bool start_match(const std::string& name) {
    using namespace detail;
    if (!initialized) {
        error("falta rnr::init() en initialize()");
        return false;
    }
    if (mode != IDLE) {
        error("ya se esta grabando o guardando");
        return false;
    }
    if (!Storage::sdcardok()) {
        error("no hay tarjeta SD");
        return false;
    }
    if (attached.empty()) {
        error("no hay objetos attacheados");
        return false;
    }
    if (!buildTable()) return false;
    uint32_t n = table.size();
    uint32_t need = sizeof(rnr::fmt::BlockHeader) + 5 + (n + 7) / 8 + 5 * n; // el peor frame debe caber
    uint32_t slot = cfg.block_bytes > need ? cfg.block_bytes : (need + 511) / 512 * 512;
    if (slot > 0xFFFF) slot = 0xFFFF;
    if (!ring.allocate(slot, cfg.ring_bytes)) {
        error("sin memoria para el buffer de grabacion");
        return false;
    }
    curPath = rnr::detail::makePath(cfg.directory, name.empty() ? "match" : name, ".match");
    st = {};
    stopReq = false;
    samplerDone = false;
    mode = MATCH;
    writerTask->notify();
    samplerTask->notify();
    return true;
}

inline bool start_macro(const std::string& name = "") {
    using namespace detail;
    if (!initialized) {
        error("falta rnr::init() en initialize()");
        return false;
    }
    if (mode != IDLE) {
        error("ya se esta grabando o guardando");
        return false;
    }
    if (!Storage::sdcardok()) {
        error("no hay tarjeta SD");
        return false;
    }
    {
        std::lock_guard<pros::Mutex> lock(*macroMutex);
        macroData.clear();
        macroData.reserve(16 * 1024);
        macroStrings.assign(1, ""); // indice 0 = sin estado
        macroLastT = 0;
        pressing.clear();
    }
    curPath = rnr::detail::makePath(cfg.directory, name.empty() ? "macro" : name, ".macro");
    st = {};
    stopReq = false;
    startUs = pros::micros();
    mode = MACRO;
    ActionManager::setRecording(true);
    return true;
}

// Para y guarda. El archivo queda listo cuando is_busy() vuelve a false (last_file() lo da).
inline void stop() {
    using namespace detail;
    uint8_t m = mode;
    if (m == IDLE || stopReq) return;
    stopReq = true;
    if (m == MACRO) {
        ActionManager::setRecording(false);
        writerTask->notify();
    }
}

// Desde una intercepcion RECORD_*: graba esta accion en lugar de la original.
// wait_prev: al reproducir, espera a que termine la accion interceptada anterior (movimientos encadenados).
inline void register_action(const std::string& action, const std::string& state = "", const Args& args = {}, bool wait_prev = true) {
    using namespace detail;
    if (mode != MACRO || stopReq) return;
    std::lock_guard<pros::Mutex> lock(*macroMutex);
    pushEvent(action, state, ActionManager::DOWN, rnr::codec::INTERCEPTED | (wait_prev ? rnr::codec::WAIT_PREV : 0), args);
}

} // namespace Recorder
