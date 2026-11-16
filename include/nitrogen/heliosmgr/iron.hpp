#pragma once
// ============================================================================
//  iron.hpp  -  ActionManager: administrador interno de las Actions
//
//  actions.generated.cpp registra todo en actionInitialization(); despues profiles,
//  hooks y el Recorder solo usan execute / trigger / finish / isFinished.
//
//  Tipos de accion:
//    TOGGLE  lista de estados. execute sin estado avanza al siguiente (el primero la
//            primera vez); execute con estado salta a ese. Corre sincrona: son instantaneas.
//    TAP     cuerpo con argumentos y fase (DOWN / PRESSING / UP). DOWN y UP corren en su
//            propio task (asincronas); PRESSING corre sincrona porque se llama cada tick.
//    LOOP    reset() al lanzar, y luego body() mientras cond() sea true, en su propio task.
//
//  Reglas:
//    * Registrar SOLO durante la inicializacion: el mapa no cambia despues y se lee sin mutex.
//    * Si una accion asincrona ya corre, execute la ignora. relaunch la repite al terminar.
//    * finish es cooperativo: un LOOP para en la siguiente vuelta, un TAP termina su cuerpo
//      (no se mata un task a medias). Un TOGGLE vuelve a "sin estado".
//    * Argumentos: siempre numeros (Args = vector<double>). Los que faltan valen 0.
// ============================================================================

#include "api.h"
#include <atomic>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ActionManager {

using Args = std::vector<double>;
using Fn = std::function<void(const Args&)>;

enum Phase { DOWN = 0, PRESSING = 1, UP = 2 };
enum class Kind { TOGGLE, TAP, LOOP };
enum class Event { STARTED, FINISHED, CANCELLED };

struct Action {
    Kind kind = Kind::TAP;

    // TOGGLE
    std::vector<void (*)()> states;
    std::vector<std::string> stateNames;
    std::atomic<int> state{-1}; // -1: sin estado (nunca corrio o se finalizo)

    // TAP
    Fn body;
    int phase = DOWN;
    int exclusive = -1; // -1: sin limite de ejecuciones
    std::atomic<int> timesRun{0};
    std::map<std::string, Fn> intercepts; // "RECORD_DOWN" | "RECORD_PRESSING" | "RECORD_UP"

    // LOOP
    void (*loopBody)() = nullptr;
    bool (*loopCond)() = nullptr;
    void (*loopReset)() = nullptr;

    // Asincrono (TAP DOWN/UP y LOOP)
    std::atomic<bool> running{false}, cancel{false}, pending{false};
    Args lastArgs;
};

inline std::map<std::string, Action>& registry() {
    static std::map<std::string, Action> r;
    return r;
}
inline std::atomic<bool> recording{false};
inline std::function<void(const std::string&, Event)> listener; // HookManager: ON STARTED / FINISHED / CANCELLED

// Recorder (modo pasivo): recibe cada trigger que el driver hizo mientras se graba una macro.
// fires = la fase coincide con la de la accion (de verdad se ejecuto).
using RecordSink = void (*)(const std::string& name, Phase phase, const Args& args, bool fires);
inline RecordSink recordSink = nullptr;

inline Action* find(const std::string& name) {
    auto it = registry().find(name);
    if (it == registry().end()) {
        printf("[iron] accion desconocida: %s\n", name.c_str());
        return nullptr;
    }
    return &it->second;
}

inline void emit(const std::string& name, Event e) {
    if (listener) listener(name, e);
}

namespace detail {
template <class... A, std::size_t... I>
void call(void (*f)(A...), const Args& a, std::index_sequence<I...>) {
    f((I < a.size() ? static_cast<A>(a[I]) : A{})...);
}

// Convierte void f(double, int, ...) en un Fn que recibe el vector de argumentos
template <class... A>
Fn wrap(void (*f)(A...)) {
    return [f](const Args& a) { call(f, a, std::index_sequence_for<A...>{}); };
}
} // namespace detail


/* ---------------------------------- REGISTRO ---------------------------------- */

inline void registerActiontoggle(const std::string& name, std::vector<void (*)()> states, std::vector<std::string> names) {
    Action& a = registry()[name];
    a.kind = Kind::TOGGLE;
    a.states = std::move(states);
    a.stateNames = std::move(names);
}

template <class... A>
void registerActionTap(const std::string& name, void (*f)(A...), int phase) {
    Action& a = registry()[name];
    a.kind = Kind::TAP;
    a.body = detail::wrap(f);
    a.phase = phase;
}

inline void registerLoop(const std::string& name, void (*body)(), bool (*cond)(), void (*reset)() = nullptr) {
    Action& a = registry()[name];
    a.kind = Kind::LOOP;
    a.loopBody = body;
    a.loopCond = cond;
    a.loopReset = reset;
}

inline void setExclusive(const std::string& name, int times) {
    if (Action* a = find(name)) a->exclusive = times;
}

template <class... A>
void setMacroInterceptation(const std::string& name, const std::string& when, void (*f)(A...)) {
    if (Action* a = find(name)) a->intercepts[when] = detail::wrap(f);
}

inline void setListener(std::function<void(const std::string&, Event)> fn) { listener = std::move(fn); }
inline void setRecording(bool on) { recording = on; }
inline void setRecordSink(RecordSink sink) { recordSink = sink; }
inline bool isRecording() { return recording; }


/* ---------------------------------- LAUNCHER ---------------------------------- */

// Lanza una accion asincrona en su propio task. false si ya estaba corriendo.
inline bool launch(const std::string& name, Action& a, const Args& args) {
    if (a.running.exchange(true)) return false;
    a.cancel = false;
    a.lastArgs = args;
    emit(name, Event::STARTED);
    pros::Task([name, &a] {
        if (a.kind == Kind::LOOP) {
            if (a.loopReset) a.loopReset();
            while (!a.cancel && a.loopCond()) {
                a.loopBody();
                pros::delay(10);
            }
        } else {
            a.body(a.lastArgs);
        }
        bool cancelled = a.cancel;
        a.running = false;
        emit(name, cancelled ? Event::CANCELLED : Event::FINISHED);
        if (a.pending.exchange(false)) launch(name, a, a.lastArgs); // relaunch pedido mientras corria
    }, name.c_str());
    return true;
}


/* ---------------------------------- CONTROL ----------------------------------- */

inline void finish(const std::string& name) {
    Action* a = find(name);
    if (!a) return;
    if (a->kind == Kind::TOGGLE) {
        a->state = -1;
        return;
    }
    a->pending = false;
    a->cancel = true;
}

// !!accion: finaliza todos los loops
inline void finishLoops() {
    for (auto& [name, a] : registry())
        if (a.kind == Kind::LOOP) finish(name);
}

// Si ya corre: la cancela y la vuelve a lanzar con los mismos argumentos al terminar
inline void relaunch(const std::string& name) {
    Action* a = find(name);
    if (!a || a->kind == Kind::TOGGLE) return;
    a->pending = true;
    a->cancel = true;
    if (!a->running && a->pending.exchange(false)) launch(name, *a, a->lastArgs);
}

inline bool isFinished(const std::string& name) {
    Action* a = find(name);
    if (!a) return true;
    return a->kind == Kind::TOGGLE ? a->state == -1 : !a->running;
}

// await !accion
inline void waitUntilFinished(const std::string& name) {
    while (!isFinished(name)) pros::delay(10);
}

// state !accion[ESTADO]
inline bool stateIs(const std::string& name, const std::string& state) {
    Action* a = find(name);
    if (!a || a->kind != Kind::TOGGLE) return false;
    int s = a->state;
    return s >= 0 && a->stateNames[s] == state;
}


/* --------------------------------- EJECUTADOR --------------------------------- */

// !accion, !accion[ESTADO], !accion(args): ejecuta ya, sin importar la fase
inline void execute(const std::string& name, const std::string& state = "", const Args& args = {}) {
    Action* a = find(name);
    if (!a) return;

    switch (a->kind) {
    case Kind::TOGGLE: {
        int next = (a->state + 1) % (int)a->states.size(); // -1 -> 0: el primer estado
        if (!state.empty()) {
            next = -1;
            for (int i = 0; i < (int)a->stateNames.size(); i++)
                if (a->stateNames[i] == state) next = i;
            if (next < 0) {
                printf("[iron] %s no tiene el estado %s\n", name.c_str(), state.c_str());
                return;
            }
        }
        a->state = next;
        a->states[next]();
        return;
    }
    case Kind::TAP:
        if (a->exclusive >= 0 && a->timesRun >= a->exclusive) return;
        if (a->phase == PRESSING) {
            a->body(args);
            a->timesRun++;
        } else if (launch(name, *a, args)) {
            a->timesRun++;
        }
        return;
    case Kind::LOOP:
        launch(name, *a, {});
        return;
    }
}

// Lo que llama el profile en cada fase del boton. Ejecuta solo si la fase coincide con la
// de la accion (TOGGLE y LOOP se lanzan en DOWN). Si se esta grabando una macro:
//   * accion con alguna intercepcion RECORD_*: corre la de esta fase (si hay) y el Recorder
//     NO graba la accion por su cuenta; la intercepcion decide que se graba (register_action).
//   * accion sin intercepciones: el Recorder la graba automaticamente.
// Solo se graba a nivel trigger (lo que pidio el driver), nunca los execute anidados.
inline void trigger(const std::string& name, Phase phase, const Args& args = {}) {
    Action* a = find(name);
    if (!a) return;
    int want = a->kind == Kind::TAP ? a->phase : DOWN;
    if (recording) {
        if (!a->intercepts.empty()) {
            static const char* key[] = {"RECORD_DOWN", "RECORD_PRESSING", "RECORD_UP"};
            auto it = a->intercepts.find(key[phase]);
            if (it != a->intercepts.end()) it->second(args);
        } else if (recordSink) {
            recordSink(name, phase, args, phase == want);
        }
    }
    if (phase == want) execute(name, "", args);
}

} // namespace ActionManager

namespace Actions = ActionManager; // hooks.generated.cpp usa Actions::
