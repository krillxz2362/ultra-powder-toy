// Перегонка. Числа обязаны совпасть с tools/still_ref.lua.
#include "upt_core.h"
#include <cstdio>
#include <string>
using namespace upt;
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::string(SUBSTANCES[i].key) == k) return i;
    return -1;
}
int main() {
    const int W = 70, H = 60;
    World s(W, H); s.rng.seed(31337);
    const int STONE=ID("STONE"), SPIRIT=ID("C_C2H5OH"), VAPOR=ID("C_C2H5OH_G"), WATER=ID("WATER");
    for (int x = 5; x <= 35; ++x) s.create(x, 45, STONE);
    for (int y = 25; y <= 44; ++y) { s.create(5, y, STONE); s.create(35, y, STONE); }
    for (int x = 5; x <= 30; ++x) s.create(x, 25, STONE);
    for (int y = 35; y <= 44; ++y) for (int x = 6; x <= 34; ++x)
        s.create(x, y, (x % 2 == 0) ? SPIRIT : WATER);
    for (int x = 36; x <= 60; ++x) s.create(x, 20, STONE);
    for (int y = 20; y <= 40; ++y) s.create(60, y, STONE);

    auto cnt = [&](int id) {
        int n = 0;
        for (int i = 0; i < s.maxUsed; ++i) if (s.alive[i] == 1 && s.type[i] == id) ++n;
        return n;
    };
    for (int step = 1; step <= 1200; ++step) {
        for (int x = 6; x <= 34; ++x) { int o = s.at(x, 44); if (o >= 0) s.tmp[o] = 90; }
        for (int x = 36; x <= 60; ++x) { int o = s.at(x, 20); if (o >= 0) s.tmp[o] = 5; }
        for (int y = 20; y <= 40; ++y) { int o = s.at(60, y); if (o >= 0) s.tmp[o] = 5; }
        s.step();
    }
    int out = 0;
    for (int i = 0; i < s.maxUsed; ++i)
        if (s.alive[i] == 1 && (s.type[i] == SPIRIT || s.type[i] == VAPOR) && s.px[i] > 35) ++out;
    std::printf("C++  конец: спирт %d  вода %d  пар %d  перегнано за куб %d\n",
                cnt(SPIRIT), cnt(WATER), cnt(VAPOR), out);
}
