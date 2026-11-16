#pragma once
// ============================================================================
//  format.hpp  -  layout binario de los archivos .match y .macro
//  C++ puro (sin PROS): lo comparte el robot y el test de PC (tests/rnr_codec_test.cpp).
//
//  Todo archivo:   FileHeader (64 B) -> tabla (tableBytes) -> datos
//
//  .match  tabla = un TableChannel por canal (objeto, perfil, canal, exp10, every, flags)
//          datos = bloques [BlockHeader 24 B + payload], cada uno decodificable solo
//                  (empieza con un keyframe). Un bloque con CRC malo o cortado se ignora.
//  .macro  tabla = strings (nombres de actions y estados; el indice 0 es "")
//          datos = eventos hasta el fin del archivo (ver codec.hpp)
//
//  Enteros en little-endian (el orden nativo del Cortex-A9 y de los PCs x86).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rnr {

// Flags de cada canal (ChannelDesc::flags)
enum ChannelFlag : uint8_t {
    APPLYABLE = 1, // el objeto sabe reproducirlo en apply()
    ANGLE = 2,     // angulo en grados*10^exp10: se interpola por el camino corto (359 -> 1 pasa por 0)
    DIGITAL = 4,   // on/off o mascara de bits: nunca se interpola, se mantiene
};

namespace fmt {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}
constexpr uint32_t FILE_MAGIC = fourcc('N', 'R', 'N', 'R');
constexpr uint32_t BLOCK_MAGIC = fourcc('B', 'L', 'K', '1');
constexpr uint16_t VERSION = 1;

enum Kind : uint8_t { MATCH = 1, MACRO = 2 };
enum HeaderFlag : uint8_t { CLEAN = 1 }; // se cerro bien: frames y durationUs son validos

struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t kind;
    uint8_t flags;
    uint32_t runId;      // de Storage::sdnextrunid()
    uint32_t periodUs;   // periodo nominal de muestreo (.match) o de replay (.macro)
    uint32_t blockBytes; // tamano maximo de un bloque (.match)
    uint32_t tableBytes;
    uint32_t tableCount; // canales (.match) o strings (.macro)
    uint32_t tableCrc;
    uint32_t frames;     // frames (.match) o eventos (.macro); valido si CLEAN
    uint32_t durationUs; // valido si CLEAN
    uint8_t reserved[20];
    uint32_t crc;        // de los 60 bytes anteriores
};
static_assert(sizeof(FileHeader) == 64, "FileHeader debe medir 64 bytes");

struct BlockHeader {
    uint32_t magic;
    uint32_t seq;      // numero de bloque, desde 0
    uint32_t frame0;   // indice global del primer frame (para la decimacion `every`)
    uint32_t t0Us;     // tiempo del primer frame desde el inicio de la grabacion
    uint16_t nFrames;
    uint16_t payloadLen;
    uint32_t crc;      // del payload
};
static_assert(sizeof(BlockHeader) == 24, "BlockHeader debe medir 24 bytes");


/* ------------------------------------ CRC32 ------------------------------------ */
// CRC32 estandar (polinomio 0xEDB88320) por tabla: el A9 no tiene instruccion CRC (llego en ARMv8).

struct Crc32Table {
    uint32_t t[256];
    constexpr Crc32Table() : t{} {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    }
};
inline constexpr Crc32Table CRC_TABLE{};

inline uint32_t crc32(const void* data, size_t n, uint32_t crc = 0) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (n--) crc = CRC_TABLE.t[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline void sealHeader(FileHeader& h) { h.crc = crc32(&h, offsetof(FileHeader, crc)); }
inline bool validHeader(const FileHeader& h) {
    return h.magic == FILE_MAGIC && h.version == VERSION && h.crc == crc32(&h, offsetof(FileHeader, crc));
}


/* ------------------------------------ TABLAS ------------------------------------ */

// Strings con largo de 1 byte (se cortan a 255)
inline void putStr(std::vector<uint8_t>& out, const std::string& s) {
    size_t n = s.size() < 255 ? s.size() : 255;
    out.push_back(uint8_t(n));
    out.insert(out.end(), s.begin(), s.begin() + n);
}
inline bool getStr(const uint8_t*& p, const uint8_t* end, std::string& s) {
    if (p >= end || end - p < 1 + *p) return false;
    s.assign(reinterpret_cast<const char*>(p + 1), *p);
    p += 1 + *p;
    return true;
}

struct TableChannel {
    std::string object, profile, channel;
    int8_t exp10 = 0;
    uint8_t every = 1;
    uint8_t flags = 0;
};

inline std::vector<uint8_t> encodeMatchTable(const std::vector<TableChannel>& t) {
    std::vector<uint8_t> out;
    for (const auto& c : t) {
        putStr(out, c.object);
        putStr(out, c.profile);
        putStr(out, c.channel);
        out.push_back(uint8_t(c.exp10));
        out.push_back(c.every);
        out.push_back(c.flags);
    }
    return out;
}

inline bool decodeMatchTable(const uint8_t* p, const uint8_t* end, uint32_t count, std::vector<TableChannel>& t) {
    t.assign(count, {});
    for (auto& c : t) {
        if (!getStr(p, end, c.object) || !getStr(p, end, c.profile) || !getStr(p, end, c.channel) || end - p < 3) return false;
        c.exp10 = int8_t(*p++);
        uint8_t every = *p++;
        c.every = every ? every : 1;
        c.flags = *p++;
    }
    return p == end;
}

inline std::vector<uint8_t> encodeStrings(const std::vector<std::string>& s) {
    std::vector<uint8_t> out;
    for (const auto& x : s) putStr(out, x);
    return out;
}

inline bool decodeStrings(const uint8_t* p, const uint8_t* end, uint32_t count, std::vector<std::string>& s) {
    s.assign(count, {});
    for (auto& x : s)
        if (!getStr(p, end, x)) return false;
    return p == end;
}

} // namespace fmt
} // namespace rnr
