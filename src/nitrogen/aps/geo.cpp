// ////////////////////////////////////////////////////////////
// //                     Jexssa 4205J                       //
// //                      Polirobots                        //
// //                    Arquitectura #6                     //
// ////////////////////////////////////////////////////////////
// //   geo.cpp - Odometria (2 ruedas de tracking + IMU)     //
// //   VERSION REVISADA                                     //
// ////////////////////////////////////////////////////////////
// //
// // CONVENCION DE EJES (igual que el IMU V5 y que draw_graph):
// //   - X global: hacia la derecha del campo
// //   - Y global: hacia adelante / arriba del campo
// //   - Heading (GlobalT): grados, HORARIO positivo (asi lo entrega el IMU V5).
// //     0 = mirando a +Y, 90 = mirando a +X.
// //
// // CONVENCION DE OFFSETS (medidos desde el CENTRO DE TRACKING, en pulgadas):
// //   - offX: distancia LATERAL de la rueda VERTICAL (sY, la que mide adelante/atras)
// //           al centro. Positivo si la rueda esta a la DERECHA del centro.
// //   - offY: distancia LONGITUDINAL de la rueda HORIZONTAL (sX, la que mide de lado)
// //           al centro. Positivo si la rueda esta ADELANTE del centro.
// //   Solo importa la distancia PERPENDICULAR a la linea en la que rueda cada rueda.
// //
// // SENTIDO DE LOS SENSORES (comprobar con el robot):
// //   - Empujar el robot hacia ADELANTE  -> sY.get_position() debe AUMENTAR
// //   - Empujar el robot hacia la DERECHA -> sX.get_position() debe AUMENTAR
// //   Si no, usar sensor.set_reversed(true) en initialize().

// #include "geo.h"
// #include "main.h"
// #include <cmath>
// using namespace pros;

// // Factor de escala del IMU. Muchos IMU V5 leen un poco menos/mas de lo real.
// // Para calibrarlo: gira el robot 10 vueltas exactas (3600 grados) a mano,
// // lee get_rotation() y pon IMU_SCALE = 3600.0 / lectura. Dejar en 1.0 si no se mide.
// static constexpr double IMU_SCALE = 1.0;

// // Mutex para que otra tarea (autonomo, PID, pantalla) no lea X de un ciclo
// // y Y de otro. Se crea la primera vez que se usa (no en el arranque estatico).
// static pros::Mutex& odomMutex() {
//     static pros::Mutex m;
//     return m;
// }

// // OJO: Rotation::get_position() devuelve CENTIgrados (1/100 de grado).
// // Por eso se divide entre 100. La funcion recibe centigrados, no grados.
// double AbsolutePositionSystem::degreesToInches(double centidegrees, double wheelDiameter) {
//     return (centidegrees / 100.0) * (M_PI * wheelDiameter) / 360.0;
// }

// double AbsolutePositionSystem::degToRad(double deg){
//     return deg * M_PI / 180.0;
// }

// // Lee los 3 sensores UNA sola vez por ciclo y devuelve el desplazamiento LOCAL
// // del centro de tracking (x = derecha del robot, y = adelante del robot).
// // Deja en prevT el heading nuevo (rad) para que update_cords use el promedio.
// AbsolutePositionSystem::TravDelta AbsolutePositionSystem::getTravDelta(){
//     int32_t rawX = sX.get_position();
//     int32_t rawY = sY.get_position();
//     double  rawT = sT.get_rotation();

//     // Si algun sensor esta desconectado o el IMU calibrando, PROS devuelve
//     // PROS_ERR / PROS_ERR_F (INFINITY). Saltamos el ciclo sin tocar nada.
//     if (rawX == PROS_ERR || rawY == PROS_ERR || !std::isfinite(rawT)) {
//         return TravDelta{ 0.0, 0.0 };
//     }

//     double pX = degreesToInches(rawX, Diameter);
//     double pY = degreesToInches(rawY, Diameter);
//     double pT = degToRad(rawT * IMU_SCALE);

//     double dX = pX - prevX;   // lo que rodo la rueda horizontal
//     double dY = pY - prevY;   // lo que rodo la rueda vertical
//     double dT = pT - prevT;   // giro del robot en este ciclo (rad, horario +)

//     // Quitar lo que las ruedas rodaron SOLO por el giro del robot.
//     // Al girar horario: una rueda vertical a la derecha va hacia atras  (-offX*dT)
//     //                   una rueda horizontal adelante va a la derecha   (+offY*dT)
//     double localX = dX - offY * dT;
//     double localY = dY + offX * dT;

//     // Metodo de arco (5225A): el robot recorre un arco, no una recta.
//     // La cuerda mide 2*sin(dT/2)/dT veces el arco.
//     if (std::fabs(dT) > 1e-9) {
//         double k = 2.0 * std::sin(dT / 2.0) / dT;
//         localX *= k;
//         localY *= k;
//     }

//     prevX = pX;
//     prevY = pY;
//     prevT = pT;
//     return TravDelta{ localX, localY };
// }

// AbsolutePositionSystem::AbsolutePositionSystem(pros::Rotation& SensorX, pros::Rotation& SensorY, pros::IMU& Inertial, double offsetX, double offsetY, double WheelDiameter)
//     : sX(SensorX)
//     , sY(SensorY)
//     , sT(Inertial)
//     , offX(offsetX)
//     , offY(offsetY)
//     , Diameter(WheelDiameter)
//     , GlobalX(0.0)
//     , GlobalY(0.0)
//     , GlobalT(0.0)
//     , prevX(0.0)
//     , prevY(0.0)
//     , prevT(0.0)        {}
// // IMPORTANTE: llamar reset_cords() despues de calibrar el IMU y ANTES de
// // empezar a llamar update_cords(); si no, el primer ciclo da un salto.

// void AbsolutePositionSystem::reset_cords(double X, double Y, double T){
//     // set_rotation falla si el IMU sigue calibrando: esperar a que termine.
//     while (sT.is_calibrating()) pros::delay(10);

//     odomMutex().take();
//     sT.set_rotation(T / IMU_SCALE);   // asi rawT*IMU_SCALE == T
//     pros::delay(20);                  // dar tiempo a que el IMU entregue el valor nuevo
//     GlobalX = X;
//     GlobalY = Y;
//     GlobalT = T;
//     prevX = degreesToInches(sX.get_position(), Diameter);
//     prevY = degreesToInches(sY.get_position(), Diameter);
//     prevT = degToRad(T);
//     odomMutex().give();
// }

// // Llamar en una tarea propia cada 10 ms (o menos).
// void AbsolutePositionSystem::update_cords(){
//     odomMutex().take();
//     double oldT = degToRad(GlobalT);         // heading al inicio del ciclo
//     TravDelta delta = getTravDelta();        // actualiza prevT con el heading nuevo
//     double newT = prevT;                     // heading al final del ciclo
//     double midT = (oldT + newT) / 2.0;       // heading PROMEDIO del ciclo

//     double c = std::cos(midT);
//     double s = std::sin(midT);
//     // Rotacion para heading HORARIO (compas): 0 = +Y, 90 = +X
//     GlobalX +=  delta.deltaX * c + delta.deltaY * s;
//     GlobalY += -delta.deltaX * s + delta.deltaY * c;
//     GlobalT  = newT * 180.0 / M_PI;          // mismo valor que se uso arriba (sin 2a lectura)
//     odomMutex().give();
// }

// AbsolutePositionSystem::Coordinates AbsolutePositionSystem::get_coordinates(){
//     odomMutex().take();
//     Coordinates c{GlobalX, GlobalY, GlobalT};
//     odomMutex().give();
//     return c;
// }

// // Dibuja el campo centrado en el robot. Llamar desde OTRA tarea, cada 50-100 ms
// // (la pantalla es lenta; no meterla en el bucle de odometria).
// // El campo va de (0,0) abajo-izquierda a (FieldSize, FieldSize) arriba-derecha.
// void AbsolutePositionSystem::draw_graph(double PositionX, double PositionY, double Heading, double Zoom, ScreenDrawSpec config){
//     int cx = config.ScreenWidth / 2;
//     int cy = config.ScreenHeight / 2;
//     double scl = config.BaseScale * Zoom;

//     pros::screen::set_pen(Color::black);
//     pros::screen::fill_rect(0, 0, config.ScreenWidth, config.ScreenHeight);

//     // En pantalla la Y crece hacia ABAJO, por eso se invierte el eje Y del campo.
//     int left   = cx - (int)(PositionX * scl);
//     int bottom = cy + (int)(PositionY * scl);
//     int size   = (int)(config.FieldSize * scl);
//     int right  = left + size;
//     int top    = bottom - size;

//     pros::screen::set_pen(Color::gray);
//     double spacing = config.FieldSize / config.GridDivisions;
//     for (int i = 1; i < config.GridDivisions; i++) {
//         int x = left + (int)(i * spacing * scl);
//         pros::screen::draw_line(x, top, x, bottom);
//         int y = bottom - (int)(i * spacing * scl);
//         pros::screen::draw_line(left, y, right, y);
//     }

//     pros::screen::set_pen(Color::white);
//     pros::screen::draw_line(left,  top,    right, top);
//     pros::screen::draw_line(left,  bottom, right, bottom);
//     pros::screen::draw_line(left,  top,    left,  bottom);
//     pros::screen::draw_line(right, top,    right, bottom);

//     pros::screen::set_pen(Color::red);
//     pros::screen::fill_circle(cx, cy, 5);

//     // Flecha de heading: 0 = arriba, 90 = derecha (coincide con el IMU)
//     double a = (90.0 - Heading) * M_PI / 180.0;
//     int endX = cx + (int)(15 * std::cos(a));
//     int endY = cy - (int)(15 * std::sin(a));
//     pros::screen::set_pen(Color::yellow);
//     pros::screen::draw_line(cx, cy, endX, endY);

//     // La fuente del Brain no tiene el simbolo de grado: se usa "deg".
//     pros::screen::set_pen(Color::white);
//     pros::screen::print(pros::E_TEXT_SMALL, 10, 10, "X: %.2f in  Y: %.2f in", PositionX, PositionY);
//     pros::screen::print(pros::E_TEXT_SMALL, 10, 25, "Heading: %.1f deg", Heading);
//     pros::screen::print(pros::E_TEXT_SMALL, 10, 40, "Zoom: %.1fx", Zoom);
// }