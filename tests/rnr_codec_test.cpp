// Test en PC del formato, codec, anillo y suavizado de recordnreplay (sin PROS).
//   g++ -std=c++20 -Wall -Wextra -O2 tests/rnr_codec_test.cpp -o rnr_test && ./rnr_test

#include "../include/nitrogen/recordnreplay/codec.hpp"
#include "../include/nitrogen/recordnreplay/ring.hpp"
#include "../include/nitrogen/recordnreplay/smoothing.hpp"
#include <climits>
#include <cstdio>
#include <random>

using namespace rnr;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                \
        }                                                              \
    } while (0)

static void testVarint() {
    const int32_t vals[] = {0, 1, -1, 63, -64, 64, 127, 128, -129, 1 << 20, INT32_MAX, INT32_MIN};
    for (int32_t v : vals) {
        uint8_t buf[5];
        uint8_t* e = codec::putVarint(buf, codec::zigzag(v));
        uint32_t u;
        const uint8_t* r = codec::getVarint(buf, e, u);
        CHECK(r == e);
        CHECK(codec::unzigzag(u) == v);
    }
    uint8_t bad[6] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x01}; // 6 bytes: invalido
    uint32_t u;
    CHECK(codec::getVarint(bad, bad + 6, u) == nullptr);
    CHECK(codec::getVarint(bad, bad + 2, u) == nullptr); // cortado
}

// Graba frames con decimacion y saltos extremos en varios bloques y los decodifica
static void testMatchRoundTrip() {
    const uint16_t N = 6;
    const uint8_t every[N] = {1, 1, 2, 10, 1, 3};
    const uint32_t BLOCK = 512, FRAMES = 3000;
    std::mt19937 rng(42);

    std::vector<std::vector<int32_t>> frames(FRAMES, std::vector<int32_t>(N));
    std::vector<uint32_t> times(FRAMES);
    int32_t cur[N] = {};
    uint32_t t = 0;
    for (uint32_t k = 0; k < FRAMES; k++) {
        t += 9000 + rng() % 3000; // 9..12 ms, como las tramas reales
        times[k] = t;
        for (int c = 0; c < N; c++) {
            if (rng() % 4 == 0) cur[c] += int32_t(rng() % 2001) - 1000;
            if (k == 500) cur[c] = c % 2 ? INT32_MAX : INT32_MIN; // saltos extremos
            if (k == 501) cur[c] = c % 2 ? INT32_MIN : INT32_MAX;
        }
        if (k > 1000 && k < 1200) for (int c = 0; c < N; c++) cur[c] = 7; // quieto: casi 0 bytes
        frames[k].assign(cur, cur + N);
    }

    // Codificar en bloques
    std::vector<std::vector<uint8_t>> blocks;
    codec::MatchEncoder enc;
    enc.setup(N, every);
    std::vector<uint8_t> buf(BLOCK);
    uint32_t seq = 0;
    enc.begin(buf.data(), BLOCK, seq, 0, times[0]);
    for (uint32_t k = 0; k < FRAMES; k++) {
        if (!enc.add(times[k], k, frames[k].data())) {
            uint32_t len = enc.finish();
            blocks.emplace_back(buf.begin(), buf.begin() + len);
            enc.begin(buf.data(), BLOCK, ++seq, k, times[k]);
            CHECK(enc.add(times[k], k, frames[k].data()));
        }
    }
    blocks.emplace_back(buf.begin(), buf.begin() + enc.finish());
    CHECK(blocks.size() > 5);

    // Lo esperado: un canal no muestreado en un frame conserva su ultimo valor muestreado
    // (el keyframe de cada bloque los toma todos)
    codec::MatchDecoder dec;
    dec.setup(N, every);
    std::vector<int32_t> held(N), got(N);
    uint32_t k = 0, gotT;
    size_t bytes = 0;
    for (auto& b : blocks) {
        bytes += b.size();
        CHECK(dec.begin(b.data(), b.size()));
        bool key = true;
        while (dec.next(gotT, got.data())) {
            for (int c = 0; c < N; c++)
                if (key || k % every[c] == 0) held[c] = frames[k][c];
            key = false;
            CHECK(gotT == times[k]);
            CHECK(got == held);
            k++;
        }
        CHECK(!dec.corrupt());
    }
    CHECK(k == FRAMES);
    printf("  match: %u frames, %d canales -> %zu bytes (%.2f B/frame, crudo %u B/frame)\n",
           FRAMES, N, bytes, double(bytes) / FRAMES, N * 4 + 4);

    // Bloque con un byte cambiado: CRC malo
    auto bad = blocks[2];
    bad[sizeof(fmt::BlockHeader) + 3] ^= 0x40;
    CHECK(!dec.begin(bad.data(), bad.size()));
    // Bloque cortado (corte de energia a mitad de escritura)
    CHECK(!dec.begin(blocks[3].data(), blocks[3].size() - 7));
    CHECK(!dec.begin(blocks[3].data(), 10));
}

static void testHeaderAndTables() {
    fmt::FileHeader h{};
    h.magic = fmt::FILE_MAGIC;
    h.version = fmt::VERSION;
    h.kind = fmt::MATCH;
    h.frames = 1234;
    fmt::sealHeader(h);
    CHECK(fmt::validHeader(h));
    h.frames++;
    CHECK(!fmt::validHeader(h));

    std::vector<fmt::TableChannel> t = {{"master", "ALL", "ly", 0, 1, APPLYABLE},
                                        {"brazo", "ACTUAL", "pos", 2, 1, APPLYABLE | ANGLE},
                                        {"brazo", "ACTUAL", "temp", 0, 50, 0}};
    auto bytes = fmt::encodeMatchTable(t);
    std::vector<fmt::TableChannel> back;
    CHECK(fmt::decodeMatchTable(bytes.data(), bytes.data() + bytes.size(), t.size(), back));
    CHECK(back.size() == 3 && back[1].object == "brazo" && back[1].exp10 == 2 && back[2].every == 50 && back[1].flags == (APPLYABLE | ANGLE));
    CHECK(!fmt::decodeMatchTable(bytes.data(), bytes.data() + bytes.size() - 1, t.size(), back));

    std::vector<std::string> s = {"", "moverAutonomo", "elevacion", "SCORE"};
    auto sb = fmt::encodeStrings(s);
    std::vector<std::string> sback;
    CHECK(fmt::decodeStrings(sb.data(), sb.data() + sb.size(), s.size(), sback) && sback == s);
}

static void testMacroEvents() {
    std::vector<codec::Event> ev = {
        {0, 1, 0, 0, codec::INTERCEPTED | codec::WAIT_PREV, {12.5, -30.25}},
        {250000, 2, 3, 0, 0, {}},
        {251000, 1, 0, 1, 0, {100, -100}},
        {900000, 1, 0, 2, 0, {}},
    };
    std::vector<uint8_t> out;
    uint32_t last = 0;
    for (auto& e : ev) codec::putEvent(out, e, last);
    std::vector<codec::Event> back;
    CHECK(codec::getEvents(out.data(), out.data() + out.size(), 4, back));
    CHECK(back.size() == ev.size());
    for (size_t i = 0; i < ev.size() && i < back.size(); i++) {
        CHECK(back[i].tUs == ev[i].tUs && back[i].action == ev[i].action && back[i].state == ev[i].state);
        CHECK(back[i].phase == ev[i].phase && back[i].flags == ev[i].flags && back[i].args == ev[i].args);
    }
    CHECK(!codec::getEvents(out.data(), out.data() + out.size() - 2, 4, back)); // cortado
    CHECK(!codec::getEvents(out.data(), out.data() + out.size(), 2, back));     // indice fuera de tabla
}

static void testRing() {
    SlotRing r;
    CHECK(r.allocate(64, 64 * 3));
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 3; i++) {
            uint8_t* s = r.acquire();
            CHECK(s);
            if (s) { s[0] = uint8_t(round * 3 + i); r.commit(); }
        }
        CHECK(r.acquire() == nullptr); // lleno
        for (int i = 0; i < 3; i++) {
            uint8_t* s = r.front();
            CHECK(s && s[0] == uint8_t(round * 3 + i));
            r.pop();
        }
        CHECK(r.front() == nullptr);
    }
}

static void testSmoothing() {
    using smooth::Interp;
    CHECK(smooth::interpolate(Interp::LINEAR, 0, 0, 0, 10, 20, 30, 0.5) == 15);
    CHECK(smooth::interpolate(Interp::HOLD, 0, 0, 0, 10, 20, 30, 0.9) == 10);
    CHECK(smooth::interpolate(Interp::LINEAR, DIGITAL, 0, 0, 1, 0, 0, 0.9) == 1);
    // Catmull-Rom sobre una recta es la misma recta y pasa por los puntos
    CHECK(std::fabs(smooth::interpolate(Interp::CUBIC, 0, 0, 0, 10, 20, 30, 0.25) - 12.5) < 1e-9);
    CHECK(smooth::interpolate(Interp::CUBIC, 0, 0, 5, 10, 40, 2, 0) == 10);
    // 350 -> 10 grados pasa por 0, no por 180
    double span = smooth::angleSpan(0);
    CHECK(std::fabs(smooth::interpolate(Interp::LINEAR, ANGLE, span, 350, 350, 10, 10, 0.5) - 0) < 1e-9);
    CHECK(std::fabs(smooth::interpolate(Interp::LINEAR, ANGLE, span, 350, 350, 10, 10, 0.25) - 355) < 1e-9);
    smooth::Ema e;
    CHECK(e.step(10, 0.5) == 10);
    CHECK(e.step(20, 0.5) == 15);
}

int main() {
    testVarint();
    testMatchRoundTrip();
    testHeaderAndTables();
    testMacroEvents();
    testRing();
    testSmoothing();
    printf(failures ? "%d FALLOS\n" : "todo OK\n", failures);
    return failures != 0;
}
