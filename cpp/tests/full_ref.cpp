// Полный шаг мира: все фазы. Числа обязаны совпасть с tools/full_ref.lua.
#include "upt_core.h"
#include <cstdio>
#include <map>
#include <string>
using namespace upt;
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::string(SUBSTANCES[i].key) == k) return i;
    return -1;
}
int main() {
    const int W = 90, H = 60;
    World s(W, H);
    s.rng.seed(4242);
    const int STONE = ID("STONE"), WOOD = ID("WOOD"), FIRE = ID("FIRE");
    const int WATER = ID("WATER"), SALT = ID("SALT"), ICE = ID("ICE"), ACID = ID("ACID");
    for (int x = 0; x < W; ++x) s.create(x, H-1, STONE);
    for (int x = 10; x <= 40; ++x) s.create(x, 40, STONE);
    for (int y = 30; y <= 39; ++y) for (int x = 12; x <= 24; ++x) s.create(x, y, WOOD);
    for (int x = 14; x <= 20; ++x) s.create(x, 29, FIRE);
    for (int y = 20; y <= 38; ++y) for (int x = 55; x <= 80; ++x) s.create(x, y, WATER);
    for (int y = 10; y <= 14; ++y) for (int x = 60; x <= 66; ++x) s.create(x, y, SALT);
    for (int y = 10; y <= 14; ++y) for (int x = 70; x <= 76; ++x) s.create(x, y, ICE);
    for (int y = 44; y <= 50; ++y) for (int x = 30; x <= 36; ++x) s.create(x, y, ACID);
    for (int i = 0; i < 400; ++i) s.step();

    int n = 0; double sx = 0, sy = 0, stp = 0;
    std::map<int,int> cnt;
    for (int i = 0; i < s.maxUsed; ++i) if (s.alive[i] == 1) {
        ++n; sx += s.px[i]; sy += s.py[i]; stp += s.tmp[i];
        ++cnt[s.type[i]];
    }
    double ap = 0, aox = 0, aat = 0;
    for (int i = 0; i < s.air.n; ++i) { ap += s.air.pv[i]; aox += s.air.ox[i]; aat += s.air.at[i]; }
    std::printf("C++  частиц %d  x %.6f  y %.6f  тепло %.6f\n", n, sx, sy, stp);
    std::printf("C++  воздух: давление %.6f кислород %.6f температура %.6f\n", ap, aox, aat);
    std::printf("C++  состав:");
    for (auto& p : cnt) std::printf(" %s=%d", SUBSTANCES[p.first].key, p.second);
    std::printf("\n");
}
