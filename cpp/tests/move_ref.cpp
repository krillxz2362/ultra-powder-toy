// Сверка фазы движения: куча песка валится на дно.
#include "upt_core.h"
#include <cstdio>
#include <string>
#include <cmath>
using namespace upt;

int main() {
    World w(60, 40);
    w.rng.seed(2463534242u);
    int sand = -1, stone = -1;
    for (int i = 0; i < SUBSTANCE_COUNT; ++i) {
        if (std::string(SUBSTANCES[i].key) == "SAND")  sand = i;
        if (std::string(SUBSTANCES[i].key) == "STONE") stone = i;
    }
    for (int x = 0; x < 60; ++x) w.create(x, 39, stone);
    for (int y = 5; y <= 14; ++y)
        for (int x = 25; x <= 34; ++x) w.create(x, y, sand);

    for (int s = 0; s < 300; ++s) { w.densities(); w.forces(); w.advect(); }

    double sy = 0.0, sx = 0.0; int n = 0, lowest = 0, settled = 0;
    for (int i = 0; i < w.maxUsed; ++i) {
        if (!w.alive[i] || w.type[i] != sand) continue;
        sx += w.px[i]; sy += w.py[i]; ++n;
        int y = (int)std::floor(w.py[i]);
        if (y > lowest) lowest = y;
        if (w.settled[i]) ++settled;
    }
    std::printf("C++   песчинок %d сумма x %.6f сумма y %.6f низ %d улеглось %d\n",
                n, sx, sy, lowest, settled);
    // профиль кучи: высота столбика в каждом третьем столбце
    std::printf("C++   профиль:");
    for (int x = 18; x < 42; x += 3) {
        int top = 40;
        for (int y = 0; y < 40; ++y) {
            int i = w.at(x, y);
            if (i >= 0 && w.type[i] == sand) { top = y; break; }
        }
        std::printf(" %d", top);
    }
    std::printf("\n");
    return 0;
}
