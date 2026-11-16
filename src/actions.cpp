

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

// // Este codigo .cpp con la sintaxis neo, se compila al final como un .cpp normal

// Action<tipo> nombre(args){
//     codigo_cpp;
// } extends MACRO {
//     codigo_macro;
// }

// Action<toggle> brake(){
//     ON:
//         motor1.brake();
//         motor2.brake();
//         motor3.brake();
//         motor4.brake();
//         motor5.brake();
//         motor6.brake();
//     OFF:
//         motor1.unbrake();
//         motor2.unbrake();
//         motor3.unbrake();
//         motor4.unbrake();
//         motor5.unbrake();
//         motor6.unbrake();
// }

// Action<toggle> elevacion(){ // puede tener mas de dos estados, y cambian secuencialmente. Tambien se puede llamar al estado especifico haciendo !action[state]
//     // si está por fuera, se ejecuta siempre:
//     finish !devolver;
//     OFF:
//         motor7.move(0);
//         motor8.move(0);
//     ABSORB:
//         motor7.move(255);
//         motor8.move(0);
//     SCORE:
//         motor7.move(255);
//         motor8.move(255);
// }

// Action<toggle> devolver(){ // tambien se pueden llamar otras acciones
//     finish !elevacion;
//     ON:
//         motor7.move(-255);
//         motor8.move(-255);
//     OFF:
//         !elevacion[OFF]
// }

// Action<tap down> iniciarPartida(){ // hay funciones que solo se ejecutan una vez al dar un botón (se selecciona si es al down o en up)
//     Profile.select("Opcontrol");
// }

// Action<exclusive 1 tap down> activarObjetoQueSoloSeUsaUnaVez(){ // los exclusive # hacen que solo se pueda ejecutar X veces
//     HerramientaSorpresaQueNosAyudaraMasTarde.on();
// }

// Action<pressing> emote(){ // esta se llaman mientras pressing... Equivalente a tap pressing
//     EmotizaInsana.emote();
// }

// Action<pressing> arcadeMove(drive, turn){ // las variables pueden declarar su tipo, o no
//     motor1.move(drive-turn);
//     motor2.move(drive-turn);
//     motor3.move(drive-turn);
//     motor4.move(drive+turn);
//     motor5.move(drive+turn);
//     motor6.move(drive+turn);
// }

// int posX;
// int posY; // se pueden poner variables como un script normal, porque en el fondo, es CPP con mas instrucciones

// Action<pressing> tankMove(left, right){
//     motor1.move(left);
//     motor2.move(left);
//     motor3.move(left);
//     motor4.move(right);
//     motor5.move(right);
//     motor6.move(right);
// } extends RECORD_DOWN { // esta vez, si se está grabando, se intercepta. RECORD_DOWN: lo que se ejecuta cuando hace down con record, RECORD_PRESSING: lo que se ejecuta mientras pressing con record, RECORD_UP: lo que se ejecuta cuando up con record
//     int posX = odometria.posX;   
//     int posY = odometria.posY;   
// } extends RECORD_UP {
//     int distanceX = odometria.posX - posX;
//     int distanceY = odometria.posY - posY;
//     Recorder.register_action(!moverAutonomo(distanceX, distanceY)); // <- Siempre se necesita hacer el register_action manual cuando se intercepta. Cuando está en un Recorder.register_action(!accion[state](argumentos)), la accion no se ejecuta, pasa de otra manera al lexer
// }

// Action moverAutonomo(distanceX, distanceY){ // Por defecto, <tap> es <tap down> y un Action sin tipo es <tap down>
//     await !tankMove; // por defecto todas las acciones son asincronas, pero podemos esperar a que una funcion termine
//     PIDLeft.move(distanceX);
//     PIDRight.move(distanceY);
//     pros::lcd::set_text(1, str(finished !tankMove)); // Tambien se puede comprobar con finished !action
// }

// Action<loop> registarCoords(){ // Tambien se puede hacer que se ejecute una accion por un tiempo indeterminado.
//     odometria.save();
// }

// // Como sabemos si es una accion o un inversor el operador "!"? Pues, una accion está sola, con awaits o expresiones especificas. Si no aplica, entonces es una variable.

// Action<loop while !finished> printearCoords(){ // While tambien aplica para el loop. Si quisiera esperar a que termine una accion, usaria while !finished !action. Un ! al finished hace que sea contrario el significado. Puede ser while x==1 o algo asi, como un if
//     print(odometria.cords());
// }

// Action<loop x times 5> printearNumeros(){ // Esto actuaria como un for. loop tambien puede tener inicializacion usando x=#. Para restar se usa -times. Si no se especifica x, por defecto es 0.
//     print(x);
// }

// // Como se desactiva el loop? !!action. El !! desactiva todos los loops