// Genera un .match de ejemplo con el codec real (para probar 3rd_party/match_viewer sin robot).
//   g++ -std=c++20 -O2 tests/make_sample_match.cpp -o make_sample && ./make_sample sample.match [expected.csv]
// El CSV opcional trae lo que se codifico (t_us,lx,ly,rx,ry,botones) para comparar con el visor.

#include "../include/nitrogen/recordnreplay/codec.hpp"
#include <cmath>
#include <cstdio>
#include <random>

using namespace rnr;

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    FILE* out = fopen(argv[1], "wb");
    FILE* csv = argc > 2 ? fopen(argv[2], "w") : nullptr;
    if (!out) return 1;

    // master: el control. brazo: un motor de mentira (pos cada frame, temp cada 50) para ejercitar `every`
    std::vector<fmt::TableChannel> table = {
        {"master", "ALL", "lx", 0, 1, APPLYABLE}, {"master", "ALL", "ly", 0, 1, APPLYABLE},
        {"master", "ALL", "rx", 0, 1, APPLYABLE}, {"master", "ALL", "ry", 0, 1, APPLYABLE},
        {"master", "ALL", "buttons", 0, 1, APPLYABLE | DIGITAL},
        {"brazo", "ACTUAL", "pos", 2, 1, APPLYABLE},  {"brazo", "ACTUAL", "temp", 0, 50, 0},
    };
    const int N = table.size();
    uint8_t every[16];
    for (int i = 0; i < N; i++) every[i] = table[i].every;
    auto tbl = fmt::encodeMatchTable(table);

    const uint32_t BLOCK = 1024, FRAMES = 1000; // 1024 B: varios bloques en pocos segundos
    std::mt19937 rng(7);
    std::vector<uint8_t> file(64 + tbl.size()), buf(BLOCK);
    std::memcpy(file.data() + 64, tbl.data(), tbl.size());

    codec::MatchEncoder enc;
    enc.setup(N, every);
    uint32_t t = 0, seq = 0;
    enc.begin(buf.data(), BLOCK, seq, 0, 0);
    for (uint32_t k = 0; k < FRAMES; k++) {
        t += 9000 + rng() % 3000;
        double s = t / 1e6;
        int32_t v[7];
        v[0] = int32_t(std::lround(127 * std::sin(s * 1.7)));         // lx
        v[1] = int32_t(std::lround(127 * std::cos(s * 1.1)));         // ly
        v[2] = int32_t(std::lround(100 * std::sin(s * 0.6)));         // rx
        v[3] = s > 2 && s < 6 ? int32_t(std::lround(127 * std::sin((s - 2) * 0.8))) : 0; // ry
        v[4] = 0;
        if (s > 1 && s < 3) v[4] |= 1 << 11;  // A
        if (s > 2 && s < 4) v[4] |= 1 << 9;   // B
        if (s > 3 && s < 8) v[4] |= 1 << 0;   // L1
        if (s > 5 && s < 6) v[4] |= 1 << 8;   // X
        if (s > 6) v[4] |= (1 << 4) | (1 << 7); // UP + RIGHT
        v[5] = int32_t(s * 4500);                                      // pos
        v[6] = 30 + int32_t(s);                                        // temp
        if (!enc.add(t, k, v)) {
            uint32_t len = enc.finish();
            file.insert(file.end(), buf.begin(), buf.begin() + len);
            enc.begin(buf.data(), BLOCK, ++seq, k, t);
            enc.add(t, k, v);
        }
        if (csv) fprintf(csv, "%u,%d,%d,%d,%d,%d,%d\n", t, v[0], v[1], v[2], v[3], v[4], v[5]);
    }
    uint32_t len = enc.finish();
    file.insert(file.end(), buf.begin(), buf.begin() + len);

    fmt::FileHeader h{};
    h.magic = fmt::FILE_MAGIC;
    h.version = fmt::VERSION;
    h.kind = fmt::MATCH;
    h.flags = fmt::CLEAN;
    h.runId = 1;
    h.periodUs = 10000;
    h.blockBytes = BLOCK;
    h.tableBytes = tbl.size();
    h.tableCount = N;
    h.tableCrc = fmt::crc32(tbl.data(), tbl.size());
    h.frames = FRAMES;
    h.durationUs = t;
    fmt::sealHeader(h);
    std::memcpy(file.data(), &h, sizeof h);

    fwrite(file.data(), 1, file.size(), out);
    fclose(out);
    if (csv) fclose(csv);
    printf("%s: %zu bytes, %u frames, %u bloques\n", argv[1], file.size(), FRAMES, seq + 1);
    return 0;
}
