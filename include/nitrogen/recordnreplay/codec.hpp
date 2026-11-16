#pragma once
// ============================================================================
//  codec.hpp  -  compresion de frames (.match) y eventos (.macro). C++ puro, sin PROS.
//
//  Frame de .match dentro de un bloque:
//    varint dtUs                       tiempo real desde el frame anterior (0 en el keyframe)
//    keyframe (1er frame del bloque):  zigzag-varint del valor absoluto de TODOS los canales
//    resto:   mascara de bits          un bit por canal que "toca" en este frame (k % every == 0),
//                                      1 = cambio
//             zigzag-varint del delta  solo de los canales que cambiaron
//  Un motor quieto cuesta 1 bit por frame. El delta se calcula en aritmetica de 32 bits sin
//  signo, asi que hasta un salto INT32_MIN -> INT32_MAX ida y vuelta es exacto.
//
//  Costo: unas pocas instrucciones enteras por canal (shift, xor, compare). No hace falta
//  ensamblador: el cuello de botella son las lecturas de sensores en sample(), no esto.
// ============================================================================

#include "format.hpp"

namespace rnr::codec {

constexpr int MAX_CHANNELS = 255;

inline uint32_t zigzag(int32_t v) { return (uint32_t(v) << 1) ^ uint32_t(v >> 31); }
inline int32_t unzigzag(uint32_t u) { return int32_t(u >> 1) ^ -int32_t(u & 1); }

inline uint8_t* putVarint(uint8_t* p, uint32_t v) {
    while (v >= 0x80) {
        *p++ = uint8_t(v) | 0x80;
        v >>= 7;
    }
    *p++ = uint8_t(v);
    return p;
}

// nullptr si el varint se sale del buffer o mide mas de 5 bytes
inline const uint8_t* getVarint(const uint8_t* p, const uint8_t* end, uint32_t& v) {
    v = 0;
    for (int s = 0; s < 35 && p < end; s += 7) {
        uint8_t b = *p++;
        v |= uint32_t(b & 0x7F) << s;
        if (!(b & 0x80)) return p;
    }
    return nullptr;
}


/* -------------------------------- ENCODER .match -------------------------------- */

class MatchEncoder {
public:
    // every[i]: decimacion de cada canal (1 = todos los frames, 10 = uno de cada 10)
    void setup(uint16_t channels, const uint8_t* every) {
        n = channels;
        for (int i = 0; i < n; i++) this->every[i] = every[i] ? every[i] : 1;
    }

    // Peor caso de un frame: dt + mascara + 5 bytes por canal
    uint32_t maxFrameBytes() const { return 5 + (n + 7) / 8 + 5u * n; }

    void begin(uint8_t* block, uint32_t capacity, uint32_t seq, uint32_t frame0, uint32_t t0Us) {
        blk = block;
        p = block + sizeof(fmt::BlockHeader);
        end = block + (capacity < 0xFFFF + sizeof(fmt::BlockHeader) ? capacity : 0xFFFF + sizeof(fmt::BlockHeader));
        h = {fmt::BLOCK_MAGIC, seq, frame0, t0Us, 0, 0, 0};
        lastT = t0Us;
    }

    bool empty() const { return h.nFrames == 0; }

    // false: no cabe; cerrar con finish() y empezar otro bloque
    bool add(uint32_t tUs, uint32_t k, const int32_t* v) {
        if (h.nFrames == 0xFFFF || uint32_t(end - p) < maxFrameBytes()) return false;
        p = putVarint(p, tUs - lastT);
        lastT = tUs;
        if (h.nFrames == 0) {
            for (int i = 0; i < n; i++) {
                p = putVarint(p, zigzag(v[i]));
                prev[i] = v[i];
            }
        } else {
            uint32_t due = 0;
            for (int i = 0; i < n; i++) due += k % every[i] == 0;
            uint8_t* mask = p;
            std::memset(mask, 0, (due + 7) / 8);
            p += (due + 7) / 8;
            uint32_t bit = 0;
            for (int i = 0; i < n; i++) {
                if (k % every[i]) continue;
                if (v[i] != prev[i]) {
                    mask[bit >> 3] |= uint8_t(1 << (bit & 7));
                    p = putVarint(p, zigzag(int32_t(uint32_t(v[i]) - uint32_t(prev[i]))));
                    prev[i] = v[i];
                }
                bit++;
            }
        }
        h.nFrames++;
        return true;
    }

    // Escribe la cabecera con el CRC y devuelve el tamano total del bloque
    uint32_t finish() {
        uint8_t* payload = blk + sizeof(fmt::BlockHeader);
        h.payloadLen = uint16_t(p - payload);
        h.crc = fmt::crc32(payload, h.payloadLen);
        std::memcpy(blk, &h, sizeof h);
        return sizeof h + h.payloadLen;
    }

private:
    uint16_t n = 0;
    uint8_t every[MAX_CHANNELS];
    int32_t prev[MAX_CHANNELS];
    uint8_t *blk = nullptr, *p = nullptr, *end = nullptr;
    fmt::BlockHeader h{};
    uint32_t lastT = 0;
};


/* -------------------------------- DECODER .match -------------------------------- */

class MatchDecoder {
public:
    void setup(uint16_t channels, const uint8_t* every) {
        n = channels;
        for (int i = 0; i < n; i++) this->every[i] = every[i] ? every[i] : 1;
    }

    static uint32_t blockSize(const uint8_t* block) {
        fmt::BlockHeader h;
        std::memcpy(&h, block, sizeof h);
        return sizeof h + h.payloadLen;
    }

    // Valida magic, largo y CRC. false = bloque corrupto o cortado: hay que saltarlo.
    bool begin(const uint8_t* block, uint32_t len) {
        i = 0;
        bad = false;
        if (len < sizeof(fmt::BlockHeader)) return fail();
        std::memcpy(&h, block, sizeof h);
        if (h.magic != fmt::BLOCK_MAGIC || sizeof h + h.payloadLen > len) return fail();
        p = block + sizeof h;
        end = p + h.payloadLen;
        if (fmt::crc32(p, h.payloadLen) != h.crc) return fail();
        t = h.t0Us;
        return true;
    }

    // Siguiente frame. Los canales que no tocan o no cambiaron conservan su valor anterior.
    bool next(uint32_t& tUs, int32_t* v) {
        if (i >= h.nFrames) return false;
        uint32_t u;
        if (!(p = getVarint(p, end, u))) return fail();
        t += u;
        if (i == 0) {
            for (int c = 0; c < n; c++) {
                if (!(p = getVarint(p, end, u))) return fail();
                prev[c] = unzigzag(u);
            }
        } else {
            uint32_t k = h.frame0 + i, due = 0;
            for (int c = 0; c < n; c++) due += k % every[c] == 0;
            const uint8_t* mask = p;
            if (uint32_t(end - p) < (due + 7) / 8) return fail();
            p += (due + 7) / 8;
            uint32_t bit = 0;
            for (int c = 0; c < n; c++) {
                if (k % every[c]) continue;
                if (mask[bit >> 3] >> (bit & 7) & 1) {
                    if (!(p = getVarint(p, end, u))) return fail();
                    prev[c] = int32_t(uint32_t(prev[c]) + uint32_t(unzigzag(u)));
                }
                bit++;
            }
        }
        std::memcpy(v, prev, n * sizeof(int32_t));
        tUs = t;
        i++;
        return true;
    }

    bool corrupt() const { return bad; }
    const fmt::BlockHeader& header() const { return h; }

private:
    uint16_t n = 0;
    uint8_t every[MAX_CHANNELS];
    int32_t prev[MAX_CHANNELS];
    const uint8_t *p = nullptr, *end = nullptr;
    fmt::BlockHeader h{};
    uint32_t t = 0, i = 0;
    bool bad = false;

    bool fail() {
        bad = true;
        h.nFrames = 0;
        return false;
    }
};


/* --------------------------------- EVENTOS .macro --------------------------------- */
//  varint dtUs | varint action | varint state | u8 (phase | flags << 2) | varint argc | argc x float32

enum EventFlag : uint8_t {
    WAIT_PREV = 1,   // antes de dispararse, espera a que termine la action interceptada anterior
    INTERCEPTED = 2, // vino de Recorder::register_action (una intercepcion RECORD_*)
};

struct Event {
    uint32_t tUs = 0;
    uint32_t action = 0, state = 0; // indices en la tabla de strings
    uint8_t phase = 0;              // 0 DOWN, 1 PRESSING, 2 UP
    uint8_t flags = 0;
    std::vector<double> args;
};

constexpr uint32_t MAX_ARGS = 64;

inline void putEvent(std::vector<uint8_t>& out, const Event& e, uint32_t& lastT) {
    uint8_t tmp[4 * 5 + 1];
    uint8_t* p = tmp;
    p = putVarint(p, e.tUs - lastT);
    lastT = e.tUs;
    p = putVarint(p, e.action);
    p = putVarint(p, e.state);
    *p++ = uint8_t(e.phase | e.flags << 2);
    uint32_t argc = e.args.size() < MAX_ARGS ? e.args.size() : MAX_ARGS;
    p = putVarint(p, argc);
    out.insert(out.end(), tmp, p);
    for (uint32_t i = 0; i < argc; i++) {
        float f = float(e.args[i]); // float32: ~7 cifras, sobra para distancias y velocidades
        uint8_t b[4];
        std::memcpy(b, &f, 4);
        out.insert(out.end(), b, b + 4);
    }
}

inline bool getEvents(const uint8_t* p, const uint8_t* end, uint32_t strings, std::vector<Event>& out) {
    out.clear();
    uint32_t t = 0, u;
    while (p < end) {
        Event e;
        if (!(p = getVarint(p, end, u))) return false;
        e.tUs = t += u;
        if (!(p = getVarint(p, end, e.action)) || !(p = getVarint(p, end, e.state)) || p >= end) return false;
        if (e.action >= strings || e.state >= strings) return false;
        e.phase = *p & 3;
        e.flags = *p++ >> 2;
        uint32_t argc;
        if (!(p = getVarint(p, end, argc)) || argc > MAX_ARGS || uint32_t(end - p) < argc * 4) return false;
        for (uint32_t i = 0; i < argc; i++, p += 4) {
            float f;
            std::memcpy(&f, p, 4);
            e.args.push_back(f);
        }
        out.push_back(std::move(e));
    }
    return true;
}

} // namespace rnr::codec
