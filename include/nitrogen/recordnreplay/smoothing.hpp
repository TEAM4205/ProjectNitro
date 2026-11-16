#pragma once
// ============================================================================
//  smoothing.hpp  -  remuestreo y suavizado del replay. C++ puro, sin PROS.
//
//  El archivo guarda los valores crudos con su timestamp real (las tramas de VEXos a veces
//  duran 4 o 6 ms en vez de 10). El replay corre en su propio reloj y calcula el valor de cada
//  canal en el instante exacto, entre los frames grabados p1 (antes) y p2 (despues):
//
//    HOLD    el valor de p1 (escalones; lo que siempre usan los canales DIGITAL)
//    LINEAR  recta entre p1 y p2
//    CUBIC   Catmull-Rom con p0..p3: pasa por los puntos grabados y su pendiente es continua
//
//  Despues, opcionalmente, un EMA (filtro exponencial) por canal para quitar ruido de sensor.
// ============================================================================

#include "format.hpp"
#include <cmath>

namespace rnr::smooth {

enum class Interp : uint8_t { HOLD, LINEAR, CUBIC };

// Una vuelta completa de un canal ANGLE en sus unidades guardadas (360 * 10^exp10)
inline double angleSpan(int8_t exp10) { return 360.0 * std::pow(10.0, exp10); }

// v llevado a la vuelta mas cercana a ref (para interpolar 359 -> 1 por 0, no por 180)
inline double unwrapNear(double ref, double v, double span) {
    double d = std::fmod(v - ref, span);
    if (d >= span / 2) d -= span;
    else if (d < -span / 2) d += span;
    return ref + d;
}

// u in [0, 1]: posicion entre p1 y p2. span solo se usa en canales ANGLE.
inline double interpolate(Interp mode, uint8_t flags, double span,
                          double p0, double p1, double p2, double p3, double u) {
    if (mode == Interp::HOLD || (flags & DIGITAL)) return p1;
    if (flags & ANGLE) {
        p0 = unwrapNear(p1, p0, span);
        p2 = unwrapNear(p1, p2, span);
        p3 = unwrapNear(p2, p3, span);
    }
    double r;
    if (mode == Interp::LINEAR) {
        r = p1 + (p2 - p1) * u;
    } else {
        double u2 = u * u, u3 = u2 * u;
        r = 0.5 * (2 * p1 + (p2 - p0) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (3 * p1 - p0 - 3 * p2 + p3) * u3);
    }
    if (flags & ANGLE) {
        r = std::fmod(r, span);
        if (r < 0) r += span;
    }
    return r;
}

// Filtro exponencial: alpha 1 = sin filtro, mas chico = mas suave (y mas atraso)
struct Ema {
    double y = 0;
    bool primed = false;
    double step(double x, double alpha) {
        if (!primed || alpha >= 1 || alpha <= 0) {
            primed = true;
            return y = x;
        }
        return y += alpha * (x - y);
    }
};

} // namespace rnr::smooth
