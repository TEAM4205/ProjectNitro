///////////////////////////////////////////////////////////////////
//   _   _ _ _                                //         __      //
//  | \ | (_) |_ _ __ ___   __ _  ___ _ __    //        / /_     //
//  |  \| | | __| '__/ _ \ / _` |/ _ \ '_  \  //       / __ \    //
//  | |\  | | |_| | | (_) | (_| |  __/ | | |  //    _ / / / /    //
//  |_| \_|_|\__|_|  \___/ \__, |\___|_| |_|  //   (_)_/ /_/     //
//                         |___/              //                 //
///////////////////////////////////////////////////////////////////
// Header de:      extconfig/ringer.cpp                          //
// Funcion:        Cola de escritura de archivos                 //
// Declaraciones:  Buffers, variables y funciones                //
///////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <iostream>

struct Data {
    std::string writeType;
    std::string filePath;

}


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
//    Script del ExtConfig encargado    // extconfig/           //
//      del la cola de escritura en     //      ringer.cpp      //
//          la memoria MicroSD          //                      //
//////////////////////////////////////////////////////////////////
// Internal Nitrogen File: ExtConfig uSD Module                 //
// Learn about nitrogen at:                                     //
//                   https://github.com/TEAM4205/ProjectNitro   //
//////////////////////////////////////////////////////////////////

/* 
[ Jexssa Override Notebook ]


    Que es un ringer?
Es un script que usa el ExtConfig para agregar a una "fila" todo lo que se va a escribir


    Cuando se usa el ringer?
En todas las escrituras pasivas, que no requieren algo inmediato (como RnR, Actions, etc)
Internamente, esto equivale a el flag EC_WRITEMODE_PASIVE (el default de cada funcion).
Para escribir inmediatamente, usa EC_WRITEMODE_ACTIVE


    Por qué usar el ringer?
El brain tiene una potencia limitada, y para no malgastar la potencia, podriamos posponer muchas escrituras
que no son inmediatas (como escritura de configuraciones, logs, actions, etc), para que se escriba solo cuando el
brain se desocupe, y no interrumpa procesos importantes, tales como calculo de PID, APS, RnR, etc.


*/

