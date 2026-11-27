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
//          Script del ExtConfig        // extconfig/           //
//     para las herramientas basicas    //        io.cpp        //
//               del modulo             //                      //
//////////////////////////////////////////////////////////////////
// Internal Nitrogen File: ExtConfig uSD Module                 //
// Learn about nitrogen at:                                     //
//                   https://github.com/TEAM4205/ProjectNitro   //
//////////////////////////////////////////////////////////////////

#include "nitrogen/extconfig/io.h"

bool ExtConfig::available() { // Verifica si existe la MicroSD
    return pros::usd::is_installed();
}

std::string ExtConfig::full_path(const char* path){ // Devuelve la ruta absoluta
    return std::string("/usd/") + path;
}

bool ExtConfig::exists(const char* path){ // Verifica que un archivo exista
    if (ExtConfig::available()){
        openfile("r");
        if (f) return false;
        fclose(f);
        return true;
    }
    return false;
}

long ExtConfig::size(const char* path){ // Devuelve el tamaño del archivo
    filenoexist(0);
    openfile("r");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    fclose(f);
    return size;
}

std::string ExtConfig::read_all_string(const char* path){
    filenoexist("");
    openfile("r");
    filedontopen("");

    long size = ExtConfig::size(path);
    
    // Si el archivo está vacio, el tamaño no se puede determinar, o es mas grande de lo que puede leer (MAX_READ), cierra el archivo
    sizevalid("");

    // Crea un string llamado out, de tamaño size (lo convierte de long a size_t), y '\0' dice que, si el tamaño no es el mismo, los espacios vacios quedan con '\0'
    std::string out(static_cast<std::size_t>(size), '\0');
    // Acá pasan dos cosas:
    // 1. Se escribe lo del archivo en out
    // 2. Se guarda el tamaño real de los archivos recibidos
    size_t n = fread(&out[0], 1, out.size(), f);
    // El tamaño se usa para resizear el archivo
    out.resize(n);

    fclose(f);
    return out;
}

std::vector<std::uint8_t> ExtConfig::read_all_bytes(const char* path){ // Devuelve todo el archivo, en bytes
    filenoexist({});
    openfile("rb");
    filedontopen({});

    long size = ExtConfig::size(path);

    sizevalid({});

    std::vector<uint8_t> out(static_cast<std::size_t>(size));

    size_t n = fread(&out[0], 1, out.size(), f);

    out.resize(n);

    fclose(f);
    return out;
}


bool ExtConfig::write_all_bytes(const char* path, const void *data, size_t len){ // Escribe en el archivo, los datos en bytes, la cantidad de datos que vos quieras (sobreescribe)
    filenoexist(false);
    openfile("wb");
    filedontopen(false);

    std::size_t n = fwrite(data, 1, len, f);

    // fclose también puede fallar al vaciar el buffer (ni idea pero le hago caso al foro)
    bool ok = (fclose(f) == 0) && (n == len);
    return ok;
}

bool ExtConfig::append_bytes(const char* path, const void *data, size_t len){ // Escribe en el archivo, los datos en bytes, la cantidad de datos que vos quieras, pero los agrega (no sobreescribe)
    filenoexist(false);
    openfile("ab");
    filedontopen(false);

    std::size_t n = fwrite(data, 1, len, f);

    // fclose también puede fallar al vaciar el buffer (ni idea pero le hago caso al foro)
    bool ok = (fclose(f) == 0) && (n == len);
    return ok;
}

bool ExtConfig::write_all_string(const char* path, const std::string &text){  // Escribe en el archivo, los datos en strings (sobreescribe)
    return ExtConfig::write_all_bytes(path, reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

bool ExtConfig::append_string(const char* path, const std::string& text){ // Escribe en el archivo, los datos en strings, pero los agrega (no sobreescribe)
    return ExtConfig::append_bytes(path, reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

bool ExtConfig::write_bytes_atomic(const char* path, const void *data, size_t len){ // Escribe en el archivo, los datos en bytes, la cantidad de datos que vos quieras, pero al modificar, primero guarda un .tmp y luego escribe bien (sobreescribe)
    filenoexist(false);

    std::string final_path(path);
    std::string temp_path = final_path + ".tmp";

    if (!write_all_bytes(temp_path.c_str(), data, len)) {
        return false;
    }

    std::remove(final_path.c_str());
    if (std::rename(temp_path.c_str(), final_path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        return false;
    }

    return true;
}

bool ExtConfig::remove(const char* path){ // Elimina un archivo
    filenoexist(false);
    return remove(path);
}

bool ExtConfig::rename(const char *from, const char *to){ // Renombra un archivo
    filenoexist(false);
    rename(from, to);
}
