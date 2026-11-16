#pragma once
// ============================================================================
//  rnr.hpp  -  Record'n'Replay de Nitrogen. Incluir este archivo y llamar rnr::init()
//  en initialize() (las tasks deben nacer ahi: las de competencia mueren al cambiar de modo).
//
//    Recorder   graba .match (Neobjects + control, modo activo) y .macro (Actions, modo pasivo)
//    Replayer   reproduce cualquiera de los dos
//
//  Archivos del modulo:
//    format.hpp     layout binario (.match / .macro), CRC32
//    codec.hpp      compresion: delta + zigzag + varint, mascara de cambios, eventos
//    smoothing.hpp  interpolacion HOLD / LINEAR / CUBIC y EMA del replay
//    ring.hpp       anillo de bloques entre tasks, sin mutex
//    norndata.hpp   interfaz NORNRDATA que implementa cada Neobject
//    recorder.hpp   namespace Recorder
//    replayer.hpp   namespace Replayer
//
//  Configuracion avanzada antes de rnr::init() (prioridades) o antes de grabar (lo demas):
//    Recorder::config().period_ms = 20;   Replayer::config().interp = Replayer::Interp::CUBIC;
// ============================================================================

#include "recorder.hpp"
#include "replayer.hpp"

namespace rnr {

inline void init() {
    Recorder::detail::init();
    Replayer::detail::init();
}

} // namespace rnr
