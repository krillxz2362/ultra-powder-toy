// Снимки сцены: ядро заполняет кадр, кадр пишется в .ppm.
// Картинки потом собирает tools/snap.py — ядру про файлы знать незачем.
#include "upt_core.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace upt;
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::string(SUBSTANCES[i].key) == k) return i;
    return -1;
}
int main() {
    const int W = 320, H = 180;
    World s(W, H);
    s.rng.seed(2026);
    const int STONE=ID("STONE"), WOOD=ID("WOOD"), FIRE=ID("FIRE"), WATER=ID("WATER");
    const int SAND=ID("SAND"), ACID=ID("ACID"), ICE=ID("ICE"), OIL=ID("OIL");

    // оттенок каждой частицы свой, иначе песок — сплошная заливка
    auto put = [&](int x, int y, int t) { s.create(x, y, t, s.rng.next(256) - 1); };

    for (int x = 0; x < W; ++x) put(x, H-1, STONE);
    for (int x = 0; x < W; ++x) put(x, H-2, STONE);
    for (int y = 120; y < H-2; ++y) { put(0, y, STONE); put(W-1, y, STONE); }
    // чаша слева: вода
    for (int y = 120; y < H-2; ++y) put(150, y, STONE);
    for (int y = 130; y < H-2; ++y) for (int x = 2; x < 150; ++x) put(x, y, WATER);
    // костёр справа под поленницей
    for (int y = 150; y < H-2; ++y) for (int x = 200; x < 280; ++x) put(x, y, WOOD);
    for (int x = 215; x < 265; ++x) put(x, 149, FIRE);
    // песок сыплется сверху
    for (int y = 10; y < 40; ++y) for (int x = 60; x < 110; ++x) put(x, y, SAND);
    // масло и кислота
    for (int y = 60; y < 80; ++y) for (int x = 180; x < 230; ++x) put(x, y, OIL);
    for (int y = 20; y < 40; ++y) for (int x = 250; x < 290; ++x) put(x, y, ACID);
    for (int y = 40; y < 60; ++y) for (int x = 20; x < 50; ++x) put(x, y, ICE);

    std::vector<uint8_t> buf(static_cast<size_t>(W) * H * 4);
    const int shots[] = {0, 60, 150, 300, 600, 1000};
    int si = 0;
    for (int step = 0; step <= 1000; ++step) {
        if (si < 6 && step == shots[si]) {
            s.fillFrame(buf.data());
            char name[64];
            std::snprintf(name, sizeof name, "/tmp/snap_%d.ppm", si);
            FILE* f = std::fopen(name, "wb");
            std::fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (size_t i = 0; i < buf.size(); i += 4) std::fwrite(&buf[i], 1, 3, f);
            std::fclose(f);
            std::printf("шаг %4d — частиц %d\n", step, s.count);
            ++si;
        }
        s.step();
    }
}
