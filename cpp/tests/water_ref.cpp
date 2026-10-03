// Сообщающиеся сосуды. Числа обязаны совпасть с tools/water_ref.lua.
#include "upt_core.h"
#include <cstdio>
#include <string>
using namespace upt;
int main() {
    const int W = 80, H = 50;
    World s(W, H);
    s.rng.seed(777);
    int ST = -1, WA = -1;
    for (int i = 0; i < SUBSTANCE_COUNT; ++i) {
        std::string k = SUBSTANCES[i].key;
        if (k == "STONE") ST = i;
        if (k == "WATER") WA = i;
    }
    for (int x = 0; x < W; ++x) s.create(x, H-1, ST);
    for (int y = 20; y <= H-2; ++y) { s.create(0, y, ST); s.create(W-1, y, ST); }
    for (int y = 20; y <= H-12; ++y) s.create(40, y, ST);
    for (int y = 21; y <= H-2; ++y) for (int x = 1; x <= 39; ++x) s.create(x, y, WA);
    for (int i = 0; i < 600; ++i) { s.liquidPressure(); s.densities(); s.forces(); s.advect(); }
    int n = 0, left = 0, right = 0; double sx = 0, sy = 0;
    for (int i = 0; i < s.maxUsed; ++i)
        if (s.alive[i] == 1 && s.type[i] == WA) {
            ++n; sx += s.px[i]; sy += s.py[i];
            if (s.px[i] < 40) ++left; else ++right;
        }
    auto topAt = [&](int x) {
        for (int y = 0; y < H; ++y) { int o = s.at(x, y); if (o >= 0 && s.type[o] == WA) return y; }
        return -1;
    };
    std::printf("C++  воды %d сумма x %.6f сумма y %.6f слева %d справа %d\n", n, sx, sy, left, right);
    std::printf("C++  верх: x=10 -> %d   x=70 -> %d\n", topAt(10), topAt(70));
}
