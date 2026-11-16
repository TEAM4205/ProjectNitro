// // Esta es la sintaxis para programar un perfil de control de robot en C++ para un robot VEX utilizando la biblioteca Nitrogen. El perfil se llama "tanque" y define cómo se comportan los motores del brazo y del sistema de intake en respuesta a las entradas del controlador.
// Profile tanque {
//     DIGITAL_A DOWN{ // Cuando se presiona el botón A, se ejecuta el motor del brazo
//         arm.execute();
//     }
//     DIGITAL_A UP{ // Cuando se suelta el botón A, se detiene el motor del brazo
//         arm.stop();
//     }
//     DIGITAL_A DOWN AND DIGITAL_B DOWN { // Se permiten comparaciones y operaciones logicas. Tambien parentesis para agrupar condiciones, manteniendo jerarquia de operaciones.
//         arm.execute();
//         intake.execute();
//     }
//     DIGITAL_A UP AND DIGITAL_B UP { // Cuando se sueltan ambos botones, se detienen ambos motores
//         arm.stop();
//         intake.stop();
//     }
//     STICK_LEFT_X DOWN [-5, 5] { // Apenas entra el STICK_LEFT_X en la zona de activación (-5 a 5), se ejecuta el motor de conducción
//         drive.brake();
//     }
//     STICK_LEFT_X UP [-5, 5] { // Cuando el STICK_LEFT_X sale de la zona de activacion (-5 a 5), se detiene el motor de conducción
//         drive.unbrake();
//     }
//     STICK_LEFT_X PRESSING ![0] { // Mientras el STICK_LEFT_X no esté en la zona de activación (0), se ejecuta el motor de conducción
//         drive.tank(VALUE_LEFT_Y, VALUE_RIGHT_Y); // Se utiliza el valor del eje Y del joystick izquierdo para controlar la velocidad de los motores izquierdo y derecho del robot
//     }
//     DIGITAL_L1 DOWN TOGGLE { // Toggle permite cambiar entre dos estados cada vez que se presiona el botón L1
//         1:
//             intake.execute();
//         2:
//             intake.stop();
//     }
//     STICK_LEFT_Y { DEADZONE 5; CURVE 1.5; SCALE 0.8; INVERT; } // Configuracion declarativa del eje: zona muerta, curva exponencial, escala e inversion. Los VALUE_LEFT_Y que se usan en las reglas ya llegan procesados
//     STICK_RIGHT_Y { DEADZONE 5; CURVE 1.5; SCALE 0.8; INVERT; }
//     DIGITAL_A DOUBLE DOWN { // DOUBLE antes de DOWN/UP: se activa con doble pulsación rápida del botón A
//         arm.execute();
//     }
//     PARTNER DIGITAL_B DOWN { // WAIT <n>ms espera ese tiempo antes de continuar con la siguiente instruccion. Si una instruccion empieza con PARTNER, significa que se ejecuta en el control del partner, si no, siempre en el Master
//         intake.execute();
//         WAIT 500ms;
//         intake.stop();
//     }
// }

// Profile precision EXTENDS tanque { // Hereda todas las reglas de "tanque". Una regla con el mismo disparador reemplaza a la del padre; el resto se mantiene
//     STICK_LEFT_Y { SCALE 0.4; } // Redefine solo la escala: modo de precision, mas lento
//     STICK_RIGHT_Y { SCALE 0.4; }
//     PARTNER DIGITAL_B DOWN { // Reemplaza la regla de B del perfil "tanque"
//         intake.stop();
//         COOLDOWN 500ms; // Se puede poner un cooldown para que no se pueda volver a ejecutar la misma accion hasta que pase ese tiempo
//     }
// }