// // ============================================================================
// //  src/hooks.cpp  -  GUIA COMPLETA DE HOOKS ("cuando pasa X, haz Y")
// //
// //  Un Hook reacciona al MUNDO (sensores, tiempo, modo de partida, fin de acciones);
// //  un Profile reacciona al CONTROL. Forma general:
// //
// //      Hook[<ambito>] nombre {
// //          <C++ normal: variables, constantes, funciones>      // locales a este hook
// //          WHEN <condicion C++> [modificadores] { ...C++... }  // reaccion a una condicion
// //          ON <evento> [modificadores] { ...C++... }           // reaccion a un evento
// //      }
// //
// //  Lo unico que NO es C++ normal:
// //     WHEN, ON y los modificadores;  AND / OR / NOT (dentro de un WHEN) = && || !
// //  El resto (if, else, for, while, switch, lambdas, funciones...) es C++ tal cual.
// //
// //  AMBITO (opcional):  Hook<driver>  Hook<autonomous>  Hook<disabled>  Hook<driver autonomous>
// //      Sin ambito = siempre activo. Al entrar a su ambito el hook arranca con estado limpio.
// //
// //  MODIFICADORES de WHEN (combinables, en cualquier orden, despues de la condicion):
// //      RISES     solo en el instante en que la condicion pasa a ser verdad   (POR DEFECTO)
// //      FALLS     solo en el instante en que deja de ser verdad
// //      WHILE     en cada evaluacion mientras sea verdad
// //      ONCE      maximo una vez por activacion del hook
// //      FOR t     la condicion debe mantenerse t antes de contar (anti-ruido)
// //      COOLDOWN t   tras disparar, ignora nuevos disparos durante t (no se encolan)
// //      EVERY t   evalua la condicion como maximo cada t (ahorra CPU en sensores lentos)
// //      Tiempos:  300ms   1s   1.5s   2min
// //
// //  EVENTOS de ON (aceptan ONCE y COOLDOWN):
// //      TIME t                       una vez, t despues de que el hook se activa
// //      EVERY t                      periodico, cada t
// //      AUTONOMOUS | DRIVER | DISABLED   al entrar a ese modo de la partida
// //      CONTROLLER_CONNECTED | CONTROLLER_DISCONNECTED
// //      STARTED !x | FINISHED !x | CANCELLED !x      ciclo de vida de una accion
// //      PROFILE "nombre"             cuando ese perfil queda activo
// //      RECORD_START | RECORD_STOP   cuando empieza / termina una grabacion
// //
// //  Utiles dentro de un hook:
// //      state !elevacion[SCORE]      true si ese toggle esta en ese estado
// //      finished !x                  true si la accion x ya termino
// //      Hook.enable("nombre") / Hook.disable("nombre") / Hook.enabled("nombre")
// //      neo::log("texto")            imprime en la terminal con la hora
// //      MasterController::rumble("- . -")   vibra el control ('.' corto, '-' largo)
// //
// //  REGLAS
// //   * El cuerpo corre en el task de hooks: NO bloquees (nada de pros::delay largos).
// //     Para cosas que tardan, lanza una accion:  !cerrarPinza;
// //   * Los hooks no usan `else`: usa FALLS, otra regla, o if/else DENTRO del cuerpo.
// //   * Orden de evaluacion: el del archivo. Si dos hooks tocan el mismo motor, gana el ultimo.
// //   * Llama a HookManager::start() en initialize().
// // ============================================================================


// // ---------------------------------------------------------------------------
// //  1) WHEN simple + FALLS: auto-captura
// //     Por defecto WHEN dispara una sola vez, al pasar a ser verdad (RISES).
// // ---------------------------------------------------------------------------
// Hook autoCaptura {
//     WHEN dist.get() < 50 {                       // llega el objeto: cerrar la pinza (1 vez)
//         !cerrarPinza;
//     }
//     WHEN dist.get() < 50 FALLS {                 // el objeto se va: abrirla
//         !abrirPinza;
//     }
// }


// // ---------------------------------------------------------------------------
// //  2) AND + FOR + COOLDOWN + variable local + if/else: anti-atasco del intake
// // ---------------------------------------------------------------------------
// Hook antiAtasco {
//     int intentos = 0;                            // variable local del hook (C++ normal)

//     // torque alto SOSTENIDO 300 ms y sin estar ya destrabando; no repetir antes de 1 s
//     WHEN intake.torque() > 2.0 AND finished !destrabar FOR 300ms COOLDOWN 1s {
//         intentos++;
//         if (intentos > 3) {                      // if / else: C++ de siempre
//             intake.stop();
//             MasterController::rumble("- - -");
//             neo::log("intake atascado: me rindo");
//         } else {
//             !destrabar;
//         }
//     }

//     WHEN intake.torque() < 0.5 {                 // se libero: reiniciar el contador
//         intentos = 0;
//     }
// }


// // ---------------------------------------------------------------------------
// //  3) WHILE + OR + ambito: limite de seguridad (vale en driver Y en autonomo)
// // ---------------------------------------------------------------------------
// Hook<driver autonomous> limiteBrazo {
//     WHEN limitSw.pressed() OR arm.position() > 95 WHILE {   // en cada ciclo mientras pase
//         finish !subirBrazo;                      // cancela la accion que lo empuja...
//         arm.stop();                              // ...y frena el motor (finish NO lo hace)
//     }
// }


// // ---------------------------------------------------------------------------
// //  4) ONCE + WHILE + EVERY: avisos de bateria
// // ---------------------------------------------------------------------------
// Hook bateria {
//     WHEN pros::battery::get_capacity() < 20 ONCE {          // una sola vez
//         MasterController::rumble(". . .");
//         neo::log("bateria baja");
//     }
//     WHEN pros::battery::get_capacity() < 10 WHILE EVERY 5s {  // insistente: cada 5 s
//         MasterController::rumble("-");
//     }
// }


// // ---------------------------------------------------------------------------
// //  5) ON TIME + Hook.disable: endgame (90 s despues de que empieza el driver)
// // ---------------------------------------------------------------------------
// Hook<driver> endgame {
//     ON TIME 90s {
//         MasterController::rumble("- - -");
//         Hook.disable("autoCaptura");             // activar/desactivar otros hooks
//         !subirBrazo(1);
//     }
// }


// // ---------------------------------------------------------------------------
// //  6) ON EVERY + variable local: telemetria periodica
// // ---------------------------------------------------------------------------
// Hook telemetria {
//     int segundos = 0;
//     ON EVERY 1s {
//         segundos++;
//         if (segundos % 10 == 0) {
//             neo::log("sigo vivo");               // cada 10 s
//         }
//     }
// }


// // ---------------------------------------------------------------------------
// //  7) Ciclo de vida de las acciones: encadenar SIN await
// // ---------------------------------------------------------------------------
// Hook cadena {
//     ON STARTED !subirBrazo   { neo::log("subiendo brazo"); }
//     ON FINISHED !subirBrazo  { !cerrarPinza; }               // al terminar de subir, cerrar
//     ON CANCELLED !subirBrazo { arm.stop(); }                 // si lo cancelan, frenar
// }


// // ---------------------------------------------------------------------------
// //  8) Modo de partida y control conectado (sin ambito: siempre activo)
// // ---------------------------------------------------------------------------
// Hook modos {
//     ON AUTONOMOUS { neo::log("autonomo"); !subirBrazo(1); }
//     ON DRIVER     { Profile.select("tanque"); }
//     ON DISABLED   { arm.stop(); intake.stop(); }
//     ON CONTROLLER_DISCONNECTED { drive.brake(); }            // se cayo el control: frenar
//     ON CONTROLLER_CONNECTED    { drive.unbrake(); }
// }


// // ---------------------------------------------------------------------------
// //  9) Perfil activo y grabacion
// // ---------------------------------------------------------------------------
// Hook avisos {
//     ON PROFILE "arcade" { MasterController::rumble("."); }
//     ON PROFILE "tanque" { MasterController::rumble(". ."); }
//     ON RECORD_START     { MasterController::rumble("-"); }
//     ON RECORD_STOP      { MasterController::rumble(". ."); }
// }


// // ---------------------------------------------------------------------------
// //  10) state !x[ESTADO]: interlock entre toggles
// // ---------------------------------------------------------------------------
// Hook interlocks {
//     WHEN state !elevacion[SCORE] WHILE {         // mientras anota, no se puede "devolver"
//         finish !devolver;
//     }
// }


// // ---------------------------------------------------------------------------
// //  11) RISES + EVERY: sensor lento, muestreado cada 50 ms
// // ---------------------------------------------------------------------------
// Hook clasificador {
//     WHEN optical.get() > 200 RISES EVERY 50ms {  // ve la pieza del rival: expulsar
//         !expulsar;
//     }
// }


// // ---------------------------------------------------------------------------
// //  12) C++ normal por todos lados: constantes, funciones, NOT
// // ---------------------------------------------------------------------------
// Hook temperaturas {
//     const double LIMITE = 55.0;                  // constante local
//     static bool caliente(double t) { return t >= LIMITE; }   // funcion local

//     WHEN (caliente(arm.temperature()) OR caliente(intake.temperature())) AND NOT limitSw.pressed()
//          FOR 2s COOLDOWN 30s {
//         for (int i = 0; i < 3; i++) {            // for normal
//             MasterController::rumble(".");
//         }
//         neo::log("motor caliente");
//     }
// }