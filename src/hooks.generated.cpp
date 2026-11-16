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


// // ============================================================================
// //  CONTRATO CON EL HookManager  (lo que este generated espera que exista)
// //
// //  Idea: igual que en profiles, el generated NO lleva contadores ni estados a mano.
// //  Cada regla se REGISTRA una vez en __hooksInit() con sus modificadores, y dentro de la
// //  funcion del hook solo se pregunta "debo disparar?". El manager guarda el estado de
// //  cada regla (flanco anterior, temporizador del FOR, cooldown, ONCE, EVERY) y lo
// //  reinicia cuando el hook entra a su ambito.
// //
// //  ID de regla = "<hook>_<n>", n = posicion (desde 1) de cada WHEN / ON dentro del hook.
// //
// //    struct HookRule { HookEdge edge = RISES; bool once = false;
// //                      uint32_t forMs = 0, cooldownMs = 0, everyMs = 0; };   // en este orden
// //    enum class HookEdge  { RISES, FALLS, WHILE };
// //    enum class HookEvent { TIME, EVERY, AUTONOMOUS, DRIVER, DISABLED, CONTROLLER_CONNECTED,
// //                           CONTROLLER_DISCONNECTED, STARTED, FINISHED, CANCELLED, PROFILE,
// //                           RECORD_START, RECORD_STOP };
// //
// //    HookManager::registerHook(nombre, funcion, reset = nullptr)
// //        funcion: se llama una vez por tick mientras el hook este en su ambito.
// //        reset:   se llama al entrar al ambito (devuelve las variables locales a su valor inicial).
// //    HookManager::editHookActivation(nombre, driver, autonomous, disabled, initialize, competition_initialize)
// //        sin esta llamada el hook esta activo siempre (los 5 en true).
// //    HookManager::registerWhen(id, HookRule)
// //    HookManager::registerOn(id, HookEvent [, ms | "nombre"] [, HookRule])
// //        TIME/EVERY llevan ms; STARTED/FINISHED/CANCELLED/PROFILE llevan el nombre.
// //
// //    bool HookManager::when(id, condicion)   la condicion es una lambda: si la regla no toca
// //                                            evaluarse (EVERY, cooldown, ONCE agotado) NO se evalua.
// //    bool HookManager::on(id)                true si el evento ocurrio / el temporizador vencio.
// //    HookManager::enable(n) / disable(n) / enabled(n)
// //
// //    Actions::execute(accion, estado, {args})   Actions::finish(accion)
// //    Actions::isFinished(accion)                Actions::stateIs(accion, estado)
// //
// //  TRADUCCION
// //    !x;  -> Actions::execute("x", "", {});   !x(1); -> ...("x", "", {1});
// //    finish !x -> Actions::finish("x")        finished !x -> Actions::isFinished("x")
// //    state !x[S] -> Actions::stateIs("x", "S")
// //    AND / OR / NOT -> && || !                Hook.enable("n") -> HookManager::enable("n")
// //    Tiempos a ms: 300ms -> 300, 1.5s -> 1500, 2min -> 120000
// //    Variables / constantes / funciones locales del hook -> namespace __<hook>_L, y la
// //    funcion del hook hace `using namespace`, asi el cuerpo se copia sin renombrar nada.
// // ============================================================================


// /* Headers */
// void __autoCaptura();
// void __antiAtasco();
// void __antiAtasco_reset();
// void __limiteBrazo();
// void __bateria();
// void __endgame();
// void __telemetria();
// void __telemetria_reset();
// void __cadena();
// void __modos();
// void __avisos();
// void __interlocks();
// void __clasificador();
// void __temperaturas();


// /* Locals */
// namespace __antiAtasco_L {
//     int intentos = 0;
// }

// namespace __telemetria_L {
//     int segundos = 0;
// }

// namespace __temperaturas_L {
//     const double LIMITE = 55.0;
//     static bool caliente(double t) { return t >= LIMITE; }
// }


// /* Init */
// void __hooksInit(){
//     HookManager::registerHook("autoCaptura", __autoCaptura);
//     HookManager::registerWhen("autoCaptura_1", {});                                  // RISES por defecto
//     HookManager::registerWhen("autoCaptura_2", {.edge = HookEdge::FALLS});

//     HookManager::registerHook("antiAtasco", __antiAtasco, __antiAtasco_reset);
//     HookManager::registerWhen("antiAtasco_1", {.forMs = 300, .cooldownMs = 1000});
//     HookManager::registerWhen("antiAtasco_2", {});

//     HookManager::registerHook("limiteBrazo", __limiteBrazo);
//     HookManager::editHookActivation("limiteBrazo", true, true, false, false, false); // driver, autonomous, disabled, initialize, competition_initialize
//     HookManager::registerWhen("limiteBrazo_1", {.edge = HookEdge::WHILE});

//     HookManager::registerHook("bateria", __bateria);
//     HookManager::registerWhen("bateria_1", {.once = true});
//     HookManager::registerWhen("bateria_2", {.edge = HookEdge::WHILE, .everyMs = 5000});

//     HookManager::registerHook("endgame", __endgame);
//     HookManager::editHookActivation("endgame", true, false, false, false, false);
//     HookManager::registerOn("endgame_1", HookEvent::TIME, 90000);

//     HookManager::registerHook("telemetria", __telemetria, __telemetria_reset);
//     HookManager::registerOn("telemetria_1", HookEvent::EVERY, 1000);

//     HookManager::registerHook("cadena", __cadena);
//     HookManager::registerOn("cadena_1", HookEvent::STARTED, "subirBrazo");
//     HookManager::registerOn("cadena_2", HookEvent::FINISHED, "subirBrazo");
//     HookManager::registerOn("cadena_3", HookEvent::CANCELLED, "subirBrazo");

//     HookManager::registerHook("modos", __modos);
//     HookManager::registerOn("modos_1", HookEvent::AUTONOMOUS);
//     HookManager::registerOn("modos_2", HookEvent::DRIVER);
//     HookManager::registerOn("modos_3", HookEvent::DISABLED);
//     HookManager::registerOn("modos_4", HookEvent::CONTROLLER_DISCONNECTED);
//     HookManager::registerOn("modos_5", HookEvent::CONTROLLER_CONNECTED);

//     HookManager::registerHook("avisos", __avisos);
//     HookManager::registerOn("avisos_1", HookEvent::PROFILE, "arcade");
//     HookManager::registerOn("avisos_2", HookEvent::PROFILE, "tanque");
//     HookManager::registerOn("avisos_3", HookEvent::RECORD_START);
//     HookManager::registerOn("avisos_4", HookEvent::RECORD_STOP);

//     HookManager::registerHook("interlocks", __interlocks);
//     HookManager::registerWhen("interlocks_1", {.edge = HookEdge::WHILE});

//     HookManager::registerHook("clasificador", __clasificador);
//     HookManager::registerWhen("clasificador_1", {.everyMs = 50});

//     HookManager::registerHook("temperaturas", __temperaturas);
//     HookManager::registerWhen("temperaturas_1", {.forMs = 2000, .cooldownMs = 30000});
// }


// /* Hook autoCaptura */
// void __autoCaptura(){
//     if (HookManager::when("autoCaptura_1", []{ return dist.get() < 50; })) {
//         Actions::execute("cerrarPinza", "", {});
//     }
//     if (HookManager::when("autoCaptura_2", []{ return dist.get() < 50; })) {
//         Actions::execute("abrirPinza", "", {});
//     }
// }


// /* Hook antiAtasco */
// void __antiAtasco_reset(){
//     __antiAtasco_L::intentos = 0;
// }

// void __antiAtasco(){
//     using namespace __antiAtasco_L;
//     if (HookManager::when("antiAtasco_1", []{ return intake.torque() > 2.0 && Actions::isFinished("destrabar"); })) {
//         intentos++;
//         if (intentos > 3) {
//             intake.stop();
//             MasterController::rumble("- - -");
//             neo::log("intake atascado: me rindo");
//         } else {
//             Actions::execute("destrabar", "", {});
//         }
//     }
//     if (HookManager::when("antiAtasco_2", []{ return intake.torque() < 0.5; })) {
//         intentos = 0;
//     }
// }


// /* Hook<driver autonomous> limiteBrazo */
// void __limiteBrazo(){
//     if (HookManager::when("limiteBrazo_1", []{ return limitSw.pressed() || arm.position() > 95; })) {
//         Actions::finish("subirBrazo");
//         arm.stop();
//     }
// }


// /* Hook bateria */
// void __bateria(){
//     if (HookManager::when("bateria_1", []{ return pros::battery::get_capacity() < 20; })) {
//         MasterController::rumble(". . .");
//         neo::log("bateria baja");
//     }
//     if (HookManager::when("bateria_2", []{ return pros::battery::get_capacity() < 10; })) {
//         MasterController::rumble("-");
//     }
// }


// /* Hook<driver> endgame */
// void __endgame(){
//     if (HookManager::on("endgame_1")) {
//         MasterController::rumble("- - -");
//         HookManager::disable("autoCaptura");
//         Actions::execute("subirBrazo", "", {1});
//     }
// }


// /* Hook telemetria */
// void __telemetria_reset(){
//     __telemetria_L::segundos = 0;
// }

// void __telemetria(){
//     using namespace __telemetria_L;
//     if (HookManager::on("telemetria_1")) {
//         segundos++;
//         if (segundos % 10 == 0) {
//             neo::log("sigo vivo");
//         }
//     }
// }


// /* Hook cadena */
// void __cadena(){
//     if (HookManager::on("cadena_1")) { neo::log("subiendo brazo"); }
//     if (HookManager::on("cadena_2")) { Actions::execute("cerrarPinza", "", {}); }
//     if (HookManager::on("cadena_3")) { arm.stop(); }
// }


// /* Hook modos */
// void __modos(){
//     if (HookManager::on("modos_1")) { neo::log("autonomo"); Actions::execute("subirBrazo", "", {1}); }
//     if (HookManager::on("modos_2")) { Profile::select("tanque"); }
//     if (HookManager::on("modos_3")) { arm.stop(); intake.stop(); }
//     if (HookManager::on("modos_4")) { drive.brake(); }
//     if (HookManager::on("modos_5")) { drive.unbrake(); }
// }


// /* Hook avisos */
// void __avisos(){
//     if (HookManager::on("avisos_1")) { MasterController::rumble("."); }
//     if (HookManager::on("avisos_2")) { MasterController::rumble(". ."); }
//     if (HookManager::on("avisos_3")) { MasterController::rumble("-"); }
//     if (HookManager::on("avisos_4")) { MasterController::rumble(". ."); }
// }


// /* Hook interlocks */
// void __interlocks(){
//     if (HookManager::when("interlocks_1", []{ return Actions::stateIs("elevacion", "SCORE"); })) {
//         Actions::finish("devolver");
//     }
// }


// /* Hook clasificador */
// void __clasificador(){
//     if (HookManager::when("clasificador_1", []{ return optical.get() > 200; })) {
//         Actions::execute("expulsar", "", {});
//     }
// }


// /* Hook temperaturas */
// void __temperaturas(){
//     using namespace __temperaturas_L;
//     if (HookManager::when("temperaturas_1", []{ return (caliente(arm.temperature()) || caliente(intake.temperature())) && !limitSw.pressed(); })) {
//         for (int i = 0; i < 3; i++) {
//             MasterController::rumble(".");
//         }
//         neo::log("motor caliente");
//     }
// }
