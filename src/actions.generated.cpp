

// //////////////////////////////////////////////////////////////////
// //   _   _ _ _                                //           ___  // 
// //  | \ | (_) |_ _ __ ___   __ _  ___ _ __    //     _   _<  /  //
// //  |  \| | | __| '__/ _ \ / _` |/ _ \ '_  \  //    | | / / /   //
// //  | |\  | | |_| | | (_) | (_| |  __/ | | |  //    | |/ / /    //
// //  |_| \_|_|\__|_|  \___/ \__, |\___|_| |_|  //    |___/_/     //
// //                         |___/              //                //
// //////////////////////////////////////////////////////////////////
// //                 Infinity 4205A, Jexssa 4205J                 //
// //                          Polirobots                          //
// //                 Arquitectura #2 - Push Back                  //
// //                            gmaxd                             //
// //////////////////////////////////////////////////////////////////
// //                                      //                      //
// //           ----------------           //     control.cpp      //
// //                                      //                      //
// //////////////////////////////////////////////////////////////////

// // Este es el translated que internamente generaría el lexer... Pero esto NUNCA lo debe ver el usuario
// // Tambien hay comentarios autogenerados, que son generados asi: /**/

// #include "main.h"
// #include "nitrogen/heliosmgr/iron.hpp"
// #include "nitrogen/recordnreplay/rnr.hpp"

// /* HEADERS */
// void __ON_brake();
// void __OFF_brake();
// void ___elevacion(); // si, por ser la que se ejecuta siempre, tiene ___ y no __
// void __OFF_elevacion();
// void __ABSORB_elevacion();
// void __SCORE_elevacion();
// void ___devolver();
// void __ON_devolver();
// void __OFF_devolver();
// void __iniciarPartida();
// void __activarObjetoQueSoloSeUsaUnaVez();
// void __emote();
// void __arcadeMove(double drive, double turn); // argumentos sin tipo -> double (el manager necesita tipos concretos)
// void __tankMove(double left, double right);
// void __ext_RECORD_DOWN__tankMove(double left, double right);
// void __ext__RECORD_UP__tankMove(double left, double right);
// void __moverAutonomo(double distanceX, double distanceY);
// void __registrarCoords();
// bool __launch_registrarCoords();
// void __printearCoords();
// bool __launch_printearCoords();
// void __printearNumeros();
// bool __launch_printearNumeros();

// /* INTERNAL VARIABLES */
// int __internalLoopState__launch_printearNumeros=0;


// /* GLOBAL FUNCTIONS */
// // si hubieran funciones de cualquier tipo, irían acá

// /* GLOBAL VARIABLES */
// int posX;
// int posY;
// bool finished;

// /* HOOK */
// void actionInitialization(){ // la primera funcion que llama el register
//     ActionManager::registerActiontoggle("brake", {__ON_brake, __OFF_brake}, {"ON", "OFF"});
//     ActionManager::registerActiontoggle("elevacion", {__OFF_elevacion, __ABSORB_elevacion, __SCORE_elevacion}, {"OFF", "ABSORB", "SCORE"});
//     ActionManager::registerActiontoggle("devolver", {__ON_devolver, __OFF_devolver}, {"ON", "OFF"});
//     ActionManager::registerActionTap("iniciarPartida", __iniciarPartida, 0); // 0: DOWN, 1: PRESSING, 2: UP
//     ActionManager::registerActionTap("activarObjetoQueSoloSeUsaUnaVez", __activarObjetoQueSoloSeUsaUnaVez, 0);
//     ActionManager::setExclusive("activarObjetoQueSoloSeUsaUnaVez", 1); // El exclusive se hace en otra instruccion
//     ActionManager::registerActionTap("emote", __emote, 1);
//     ActionManager::registerActionTap("arcadeMove", __arcadeMove, 1);
//     ActionManager::registerActionTap("tankMove", __tankMove, 1);
//     ActionManager::setMacroInterceptation("tankMove", "RECORD_DOWN", __ext_RECORD_DOWN__tankMove); // La interceptacion tambien en otra sentencia
//     ActionManager::setMacroInterceptation("tankMove", "RECORD_UP", __ext__RECORD_UP__tankMove);
//     ActionManager::registerActionTap("moverAutonomo", __moverAutonomo, 0);
//     ActionManager::registerLoop("registrarCoords", __registrarCoords, __launch_registrarCoords);
//     ActionManager::registerLoop("printearCoords", __printearCoords, __launch_printearCoords);
//     ActionManager::registerLoop("printearNumeros", __printearNumeros, __launch_printearNumeros, []{ __internalLoopState__launch_printearNumeros = 0; }); // el reset devuelve x a su valor inicial en cada lanzamiento
// }


// /* Action<toggle> brake */
// void __ON_brake(){
//     motor1.brake();
//     motor2.brake();
//     motor3.brake();
//     motor4.brake();
//     motor5.brake();
//     motor6.brake();
// }

// void __OFF_brake(){
//     motor1.unbrake();
//     motor2.unbrake();
//     motor3.unbrake();
//     motor4.unbrake();
//     motor5.unbrake();
//     motor6.unbrake();
// }


// /* Action<toggle> elevacion */
// void ___elevacion(){
//     ActionManager::finish("devolver");
// }

// void __OFF_elevacion(){
//     ___elevacion();
//     motor7.move(0);
//     motor8.move(0);
// }

// void __ABSORB_elevacion(){
//     ___elevacion();
//     motor7.move(255);
//     motor8.move(0);
// }

// void __SCORE_elevacion(){
//     ___elevacion();
//     motor7.move(255);
//     motor8.move(255);
// }


// /* Action<toggle> devolver */
// void ___devolver(){
//     ActionManager::finish("elevacion"); // en actions.cpp es `finish !elevacion`
// }

// void __ON_devolver(){
//     ___devolver();
//     motor7.move(-255);
//     motor8.move(-255);
// }

// void __OFF_devolver(){
//     ___devolver();
//     ActionManager::execute("elevacion", "OFF");
// }


// /* Action<tap down> iniciarPartida */
// void __iniciarPartida(){
//     Profile::select("Opcontrol");
// }


// /* Action<exclusive 1 tap down> activarObjetoQueSoloSeUsaUnaVez */
// void __activarObjetoQueSoloSeUsaUnaVez(){
//     HerramientaSorpresaQueNosAyudaraMasTarde.on();
// }

// /* Action<pressing> emote */
// void __emote(){
//     EmotizaInsana.emote();
// }


// /* Action<pressing> arcadeMove(drive, turn) */
// void __arcadeMove(double drive, double turn){
//     motor1.move(drive-turn);
//     motor2.move(drive-turn);
//     motor3.move(drive-turn);
//     motor4.move(drive+turn);
//     motor5.move(drive+turn);
//     motor6.move(drive+turn);
// }


// /* Action<pressing> tankMove(left, right) */
// void __tankMove(double left, double right){
//     motor1.move(left);
//     motor2.move(left);
//     motor3.move(left);
//     motor4.move(right);
//     motor5.move(right);
//     motor6.move(right);
// }

// void __ext_RECORD_DOWN__tankMove(double left, double right){
//     int posX = odometria.posX;   
//     int posY = odometria.posY;
// }

// void __ext__RECORD_UP__tankMove(double left, double right){
//     double distanceX = odometria.posX - posX; // double: los args de las acciones son double
//     double distanceY = odometria.posY - posY;
//     Recorder::register_action("moverAutonomo", "", {distanceX, distanceY}); // es: register_action(accion, state (si no aplica, ""), y los argumentos)
// }


// /* Action moverAutonomo(double distanceX, double distanceY) */
// void __moverAutonomo(double distanceX, double distanceY){
//     ActionManager::waitUntilFinished("tankMove");
//     PIDLeft.move(distanceX);
//     PIDRight.move(distanceY);
//     pros::lcd::set_text(1, str(ActionManager::isFinished("tankMove")));
// }


// /* Action<loop> registarCoords */
// void __registrarCoords(){
//     odometria.save();
// }

// bool __launch_registrarCoords(){ // Por que? porque desde acá es el while interno
//     return true;
// }


// /* Action<loop while !finished> printearCoords */
// void __printearCoords(){
//     print(odometria.cords());
// }

// bool __launch_printearCoords(){
//     return !finished;
// }


// /* Action<loop x times 5> printearNumeros */
// void __printearNumeros(){
//     print(__internalLoopState__launch_printearNumeros);
// }

// bool __launch_printearNumeros(){
//     return __internalLoopState__launch_printearNumeros++ < 5; // true = seguir; antes devolvia true al TERMINAR
// }

