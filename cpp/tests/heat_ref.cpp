// Прогон фазы теплообмена и выгрузка температур — для сверки с Lua.
#include <string>
#include "upt_core.h"
#include <cstdio>
#include <cmath>
using namespace upt;

int main() {
    World w(60, 40);
    // Полоса воды, под ней металл, слева камень. Горячее пятно в металле.
    int idWater = -1, idMetal = -1, idStone = -1;
    for (int i = 0; i < SUBSTANCE_COUNT; ++i) {
        if (std::string(SUBSTANCES[i].key) == "WATER") idWater = i;
        if (std::string(SUBSTANCES[i].key) == "METAL") idMetal = i;
        if (std::string(SUBSTANCES[i].key) == "STONE") idStone = i;
    }
    for (int y = 10; y <= 25; ++y)
        for (int x = 5; x <= 50; ++x) w.create(x, y, idWater);
    for (int x = 5; x <= 50; ++x) w.create(x, 26, idMetal);
    for (int y = 10; y <= 26; ++y) w.create(4, y, idStone);
    for (int x = 20; x <= 30; ++x) {
        int i = w.at(x, 26);
        if (i >= 0) w.tmp[i] = 500.0;
    }
    // Все куски считаем тепловыми, как делает Lua перед первым шагом.
    for (auto& t : w.therm) t = AWAKE;

    for (int step = 0; step < 200; ++step) w.heat();

    double sum = 0.0, mx = -1e9, mn = 1e9;
    int n = 0;
    for (int i = 0; i < w.maxUsed; ++i) {
        if (!w.alive[i]) continue;
        sum += w.tmp[i]; ++n;
        if (w.tmp[i] > mx) mx = w.tmp[i];
        if (w.tmp[i] < mn) mn = w.tmp[i];
    }
    std::printf("C++   частиц %d сумма %.6f среднее %.6f макс %.6f мин %.6f\n",
                n, sum, sum / n, mx, mn);
    // точечные пробы
    int probes[5][2] = {{25,26},{25,24},{25,20},{25,15},{10,20}};
    for (auto& p : probes) {
        int i = w.at(p[0], p[1]);
        std::printf("C++   проба %2d,%2d = %.6f\n", p[0], p[1], i >= 0 ? w.tmp[i] : 0.0);
    }
    return 0;
}
