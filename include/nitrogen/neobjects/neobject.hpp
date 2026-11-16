#pragma once
// Clase base de todos los Neobjects. Solo la interfaz: cada objeto concreto (motor, IMU...)
// decide sus perfiles de grabacion implementando NORNRDATA.

#include "nitrogen/recordnreplay/norndata.hpp"

class Neobject {
public:
    virtual ~Neobject() = default;
    // Unico entre los objetos del robot: identifica al objeto dentro de los .match
    virtual const char* name() const = 0;
    virtual NORNRDATA& rnr() = 0;
};
