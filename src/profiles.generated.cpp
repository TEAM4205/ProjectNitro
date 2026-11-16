// // Esta es la traspilación de los perfiles de control de robot en C++ para un robot VEX utilizando la biblioteca Nitrogen. El perfil se llama "tanque" y define cómo se comportan los motores del brazo y del sistema de intake en respuesta a las entradas del controlador.
// // Todas las entradas se leen de la foto que toma el ProfileManager al inicio de cada tick (carbon.hpp),
// // nunca directo del pros::Controller: get_digital_new_press solo devuelve true a la primera llamada.

// #include "main.h"
// #include "nitrogen/heliosmgr/carbon.hpp"

// void __tanque();
// void __precision();

// pros::Controller _ControllerMaster(pros::E_CONTROLLER_MASTER);
// pros::Controller _ControllerPartner(pros::E_CONTROLLER_PARTNER);



// void profilesInitialization(){ // la primera funcion que llama el register
//     Profile::registerProfile("tanque", &__tanque);
//     Profile::registerProfile("precision", &__precision);
//     Profile::registerController(&_ControllerMaster, &_ControllerPartner);
//     Profile::registerRangeListener("STICK_LEFT_X_-5_5", pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_LEFT_X, -5, 5); // Para cada rango existente se registra un listener: ID, control, eje, limite inferior y superior (inclusivos)
//     Profile::registerRangeListener("STICK_LEFT_X_0_0", pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_LEFT_X, 0, 0);
//     Profile::registerDoubleListener("DIGITAL_A_DOUBLE_DOWN", pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A, ProfileManager::DOWN); // Para cada double existente: ID, control, boton y flanco (DOWN o UP)
// }


// void __tanque(){
//     // Primero se ejecutan las configuraciones (idempotentes: se pueden repetir cada tick)
//     Profile::setAxisConfig(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_LEFT_Y, {5, 1.5, 0.8, true}); // deadzone, curve, scale, invert, [slew]
//     Profile::setAxisConfig(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_RIGHT_Y, {5, 1.5, 0.8, true});
//     Profile::addToggle("DIGITAL_L1_1", 2); // Es un contador basicamente, se almacena siempre BTN_NUM, por si hay mas toggles, poder diferenciarlos. El segundo argumento es la cantidad de estados que tiene el toggle, en este caso 2 (ON y OFF)
//     // Luego se ejecutan las reglas de control
//     if (Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A)) {
//         arm.execute();
//     }
//     if (Profile::newRelease(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A)) {
//         arm.stop();
//     }
//     if (Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A) && Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_B)) {
//         arm.execute();
//         intake.execute();
//     }
//     if (Profile::newRelease(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A) && Profile::newRelease(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_B)) {
//         arm.stop();
//         intake.stop();
//     }
//     if (Profile::analogNewPress("STICK_LEFT_X_-5_5")) {
//         drive.brake();
//     }
//     if (Profile::analogNewRelease("STICK_LEFT_X_-5_5")) {
//         drive.unbrake();
//     }
//     if (Profile::analogNotIn("STICK_LEFT_X_0_0")) {
//         drive.tank(Profile::value(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_LEFT_Y), Profile::value(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_RIGHT_Y));
//     }
//     if (Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_L1)) {
//         if (Profile::getToggle("DIGITAL_L1_1") == 1) {
//             intake.execute();
//         } else if (Profile::getToggle("DIGITAL_L1_1") == 2) {
//             intake.stop();
//         }
//         Profile::incrementToggle("DIGITAL_L1_1");
//     }
//     if (Profile::doubled("DIGITAL_A_DOUBLE_DOWN")) {
//         arm.execute();
//     }
//     if (Profile::newPress(pros::E_CONTROLLER_PARTNER, pros::E_CONTROLLER_DIGITAL_B)) {
//         intake.execute();
//         pros::delay(500);
//         intake.stop();
//     }
// }

// // un EXTENDS es basicamente un copypaste de la funcion heredada
// void __precision(){
//     // Primero se ejecutan las configuraciones. EXTENDS solo redefine SCALE: el resto se hereda de "tanque"
//     Profile::setAxisConfig(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_LEFT_Y, {5, 1.5, 0.4, true});
//     Profile::setAxisConfig(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_RIGHT_Y, {5, 1.5, 0.4, true});
//     Profile::addToggle("DIGITAL_L1_1", 2); // Es un contador basicamente, se almacena siempre BTN_NUM, por si hay mas toggles, poder diferenciarlos. El segundo argumento es la cantidad de estados que tiene el toggle, en este caso 2 (ON y OFF)
//     Profile::addCooldown("PARTNER_DIGITAL_B_1", 500); // Se puede poner un cooldown para que no se pueda volver a ejecutar la misma accion hasta que pase ese tiempo. El primer argumento es el ID del cooldown, el segundo es el tiempo en milisegundos
//     // Luego se ejecutan las reglas de control
//     if (Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A)) {
//         arm.execute();
//     }
//     if (Profile::newRelease(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A)) {
//         arm.stop();
//     }
//     if (Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A) && Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_B)) {
//         arm.execute();
//         intake.execute();
//     }
//     if (Profile::newRelease(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_A) && Profile::newRelease(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_B)) {
//         arm.stop();
//         intake.stop();
//     }
//     if (Profile::analogNewPress("STICK_LEFT_X_-5_5")) {
//         drive.brake();
//     }
//     if (Profile::analogNewRelease("STICK_LEFT_X_-5_5")) {
//         drive.unbrake();
//     }
//     if (Profile::analogNotIn("STICK_LEFT_X_0_0")) {
//         drive.tank(Profile::value(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_LEFT_Y), Profile::value(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_ANALOG_RIGHT_Y));
//     }
//     if (Profile::newPress(pros::E_CONTROLLER_MASTER, pros::E_CONTROLLER_DIGITAL_L1)) {
//         if (Profile::getToggle("DIGITAL_L1_1") == 1) {
//             intake.execute();
//         } else if (Profile::getToggle("DIGITAL_L1_1") == 2) {
//             intake.stop();
//         }
//         Profile::incrementToggle("DIGITAL_L1_1");
//     }
//     if (Profile::doubled("DIGITAL_A_DOUBLE_DOWN")) {
//         arm.execute();
//     }9
//     if (Profile::newPress(pros::E_CONTROLLER_PARTNER, pros::E_CONTROLLER_DIGITAL_B) && Profile::cooldown("PARTNER_DIGITAL_B_1")) { // el cooldown va despues del && para que solo se consuma si se presiono
//         intake.stop(); // Acá se modificó
//     }
// }
