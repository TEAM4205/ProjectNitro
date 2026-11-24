#pragma once
#include "api.h"
#include <vector>

// Vision Sensor estandar (no el AI) para Override 2026-27.
// El sensor solo entrega cajas por color (firma). Un pin son dos cajas que se tocan (amarillo-rojo, azul-rojo...),
// en cualquier angulo. La cercania sale del tamano aparente; la distancia en cm, de una calibracion.
namespace ovision {

enum Sig { RED = 1, BLUE = 2, YELLOW = 3 }; // ids de firma en el sensor (iguales que en tools/identificar_pines.py)

struct Pin {
	int sigA, sigB;      // firmas de las dos mitades (sigB = 0: solo se ve una mitad)
	int x, y, w, h;      // caja del pin completo en pixeles del sensor (316x212)
	int largo;           // lado mayor de la caja: sirve igual con el pin parado o inclinado
	double cm;           // distancia; -1 sin calibrar
	int cercania;        // 0 (nada / lejos) .. 100 (pegado a la camara), suavizada (solo en closest)
	const char* nombre;  // "amarillo-rojo", "azul-rojo", "?-rojo" (la otra mitad no se ve)...
};

void init(int port);                     // carga /usd/vision.txt y manda las firmas al sensor
std::vector<Pin> detect();               // pines a la vista, el mas grande (cercano) primero
bool closest(Pin& out);                  // pin mas cercano con cercania y distancia suavizadas; false si no se ve
bool calibrate(double cm);               // el pin mas grande esta a cm de la camara: guarda la constante de distancia
void tuneMode(pros::Controller& master); // ajuste de firmas en vivo con dibujo en el brain; bloquea hasta B

} // namespace ovision
