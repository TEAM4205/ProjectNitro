///////////////////////////////////////////////////////////////////
//   _   _ _ _                                //         __      //
//  | \ | (_) |_ _ __ ___   __ _  ___ _ __    //        / /_     //
//  |  \| | | __| '__/ _ \ / _` |/ _ \ '_  \  //       / __ \    //
//  | |\  | | |_| | | (_) | (_| |  __/ | | |  //    _ / / / /    //
//  |_| \_|_|\__|_|  \___/ \__, |\___|_| |_|  //   (_)_/ /_/     //
//                         |___/              //                 //
///////////////////////////////////////////////////////////////////
// Header de:      extconfig/io.cpp                              //
// Funcion:        Utilidades basicas de la librería             //
// Declaraciones:  definiciones y funciones                      //
///////////////////////////////////////////////////////////////////

#include <cstddef>
#include <cstdio>
#include "main.h"
#include "pros/misc.hpp"

// Utildefines
#define openfile(mode) FILE *f = fopen(path, mode)

#define filenoexist(value) if (!ExtConfig::available()) return value

#define filedontopen(value) if (!f) return value

#define sizevalid(value) if(size <= 0 || static_cast<std::size_t>(size) > MAX_READ){   \
                            fclose(f);                                                 \
                            return value;}



// Namespace
namespace ExtConfig {

    // Constantes
    constexpr std::size_t MAX_READ = 64 * 1024; // Buffer maximo de lectura

    // Funciones
    bool available(); // Verifica si existe la MicroSD
    std::string full_path(const char* path); // Devuelve la ruta absoluta

    bool exists(const char* path); // Verifica que un archivo exista
    long size(const char* path); // Devuelve el tamaño del archivo
    
    std::string read_all_string(const char* path); // Devuelve todo el archivo, en string  
    std::vector<std::uint8_t> read_all_bytes(const char* path); // Devuelve todo el archivo, en bytes

    bool write_all_bytes(const char* path, const void* data, size_t len); // Escribe en el archivo, los datos en bytes, la cantidad de datos que vos quieras (sobreescribe)
    bool append_bytes(const char* path, const void* data, size_t len); // Escribe en el archivo, los datos en bytes, la cantidad de datos que vos quieras, pero los agrega (no sobreescribe)
    bool write_all_string(const char* path, const std::string& text); // Escribe en el archivo, los datos en strings (sobreescribe)
    bool append_string(const char* path, const std::string& text); // Escribe en el archivo, los datos en strings, pero los agrega (no sobreescribe)
    bool write_bytes_atomic(const char* path, const void* data, size_t len); // Escribe en el archivo, los datos en bytes, la cantidad de datos que vos quieras, pero al modificar, primero guarda un .tmp y luego escribe bien (sobreescribe)

    bool remove(const char* path); // Elimina un archivo
    bool rename(const char* from, const char* to); // Renombra un archivo
}
