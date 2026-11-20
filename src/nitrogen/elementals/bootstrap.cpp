//////////////////////////////////////////////////////////////////
//   _   _ _ _                                //         _____  // 
//  | \ | (_) |_ _ __ ___   __ _  ___ _ __    //   _   _|__  /  //
//  |  \| | | __| '__/ _ \ / _` |/ _ \ '_  \  //  | | / //_ <   //
//  | |\  | | |_| | | (_) | (_| |  __/ | | |  //  | |/ /__/ /   //
//  |_| \_|_|\__|_|  \___/ \__, |\___|_| |_|  //  |___/____/    //
//                         |___/              //                //
//////////////////////////////////////////////////////////////////
//                 Infinity 4205A, Jexssa 4205J                 //
//                          Polirobots                          //
//                  Arquitectura #3 - Override                  //
//                            gmaxd                             //
//////////////////////////////////////////////////////////////////
//                                      //                      //
//           ----------------           //     control.cpp      //
//                                      //                      //
//////////////////////////////////////////////////////////////////
// Internal Nitrogen File: Module initializer script            //
// Learn about nitrogen at:                                     //
//                   https://github.com/TEAM4205/ProjectNitro   //
//////////////////////////////////////////////////////////////////

/* 
[ Jexssa Override Notebook ]

    Que es el bootstrap?
El bootstrap es un script que se ejecuta al iniciar el robot, y es el que da la secuencia de inicializacion
y configuracion de los modulos. Es el primer script que se ejecuta, ya que funciona como un "lanzador" (launcher)
de los diferentes subsistemas.

Parte de un nivel base y va escalando las capas, cargando los modulos, y conectandolos, verificando que todos funcionen
correctamente. Si se detecta una incoherencia, en ciertos casos puede abrir un menu de boot option, o abortar la
ejecucion de los modulos de capas superiores, dejandolo en un modo seguro para el funcionamiento elemental del robot.


    Contexto
Nitrogen está dividido en modulos, subsistemas que se encargan de funciones especificas. Cada modulo tiene su propio
script y funcionamiento, y muchos de ellos se adhieren al sistema de hooks, o corren en una tarea de hilo independiente.
Para que todo esto se active, cada modulo tiene que inicializarse con ::initialize(), y para eso, el bootstrap es el
que se encarga de llamar a todos los modulos, en el orden y con las dependencias correctas.

Asimismo, se encarga de otras tareas secundarias (como los .runid y logs) complementarias del momento del booteo.
Esto con el fin de automatizar la inicializacion del robot, y que el usuario no tenga que preocuparse de nada relacionado
con la plantilla, y pueda enfocarse en la logica de su programa.


    Punto de entrada
En initialize, se llama a Nitrogen::initialize() (de Nitrogen.cpp, el script umbrella), que a su vez llama a bootstrap.cpp.
Bootstrap.cpp es el encargado de desplegar toda la secuencia y capas de los modulos.

Esta secuencia corre en el hilo principal de PROS.
Corre durante el initialize() de PROS. Mientras el initialize() de PROS no termine todas sus tareas, no entra en ninguna
de las otras fases (autonomous, opcontrol, etc). Por eso nunca queda limitado nada.
Duracion aproximada de 1-2 segundos.

El bootstrap se descarga completamente al terminar, retornando 0 y retomando a la fase seleccionada. Todo se loggea en
usd/logs/boot/{runid}_boot.log.


    Modos de arranque
El robot tiene diferentes modos de arranque, esto tanto para poder analizar y tomar control de la operacion, como para tener un
respaldo (modo binary) en tal caso los modulos fallen, dejando un sistema minimo vivo en momento de competencia.

El modo de arranque normal desactiva la pantalla completamente y corre silenciosamente, sin mostrar ningun log si no es
necesario.

El modo de arranque normal con pantalla de carga es un modo de arranque que muestra un log. Está desactivado por defecto
debido al rendimiento, sin embargo, puede activarse en el .ipc (Initial Program Config), o en el constructor de
Nitrogen::initialize().

El modo de preboot ocurre cuando el arranque falla en un punto y el bootstrap tiene ambiguedades. En este modo, el bootstrap
muestra, en LLEMU, un menú de opciones para que el usuario seleccione que hacer. Luego de esto, regresa al arranque normal.

El modo de arranque binario ocurre cuando el arranque falla en un punto, o se decide forzar el arranque binario. En este modo,
el bootstrap aborta todas las tareas y hooks de los modulos hipervisores, y deja el sistema minimo vivo, junto con los modulos
pasivos en su estado desacoplado. Esto permite que el robot siga funcionando, usando el codigo nativo de PROS.


    Secuencia de arranque
1. Comprobar la MicroSD, que es la que contiene muchos archivos necesarios para la plantilla. En ausencia de esta, se bootea
   en modo binario.
2. Carga ExtConfig para la lectura de los archivos siguientes.
3. Lee .ipc y carga la configuracion de arranque general y especifica.
4. Carga los modulos y subsistemas, capa a capa.
5. Precarga lo elemental en RAM: perfiles, modos de control, autonomos, acciones, hooks, etc.
6. Apaga LLEMU (si aplica) y activa Molotov
7. Termina de escribir el .ipc (si aplica) y retorna 0
*/

#include "liblvgl/llemu.hpp"
#include "nitrogen/elementals/native_menu_mgr.hpp"
#include "main.h"

namespace Bootstrap {

}