// Плавление и застывание: брусок железа греем, потом даём остыть.
// И отдельно — насколько быстро тепло идёт по бруску.
#include "upt_core.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace upt;
static int fails = 0;
static void ok(bool c, const char* what, const std::string& got = "") {
    std::printf("%s %s %s\n", c ? "[ОК]  " : "[ПЛОХО]", what, got.c_str());
    if (!c) ++fails;
}
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::string(SUBSTANCES[i].key) == k) return i;
    return -1;
}
static int countOf(World& s, int t) {
    int n = 0;
    for (int i = 0; i < s.maxUsed; ++i) if (s.alive[i] == 1 && s.type[i] == t) ++n;
    return n;
}

int main() {
    const int FE = ID("FE"), FE_L = ID("FE_L"), ST = ID("STONE");
    ok(FE >= 0 && FE_L >= 0, "железо и его расплав есть в списке веществ");

    // Брусок железа в каменной чаше.
    const int W = 60, H = 40;
    World s(W, H);
    s.rng.seed(55);
    for (int x = 0; x < W; ++x) { s.create(x, H - 1, ST); s.create(x, H - 2, ST); }
    for (int y = 20; y < H - 2; ++y) { s.create(10, y, ST); s.create(49, y, ST); }
    int made = 0;
    for (int y = 26; y < H - 2; ++y)
        for (int x = 12; x < 48; ++x)
            if (s.create(x, y, FE) >= 0) ++made;
    ok(made > 300, "брусок набран", std::to_string(made) + " частиц");

    // Греем снизу: как горелкой.
    for (int step = 0; step < 900; ++step) {
        for (int x = 12; x < 48; ++x) {
            const int o = s.at(x, H - 3);
            if (o >= 0) s.tmp[o] = 2000.0;
        }
        s.step();
    }
    const int molten = countOf(s, FE_L), solid = countOf(s, FE);
    ok(molten > 0, "железо расплавилось",
       "расплава " + std::to_string(molten) + ", твёрдого " + std::to_string(solid));

    // Теперь остужаем: расплав обязан застыть обратно в железо.
    for (int i = 0; i < s.maxUsed; ++i)
        if (s.alive[i] == 1 && (s.type[i] == FE_L || s.type[i] == FE)) s.tmp[i] = 20.0;
    for (int step = 0; step < 600; ++step) {
        for (int i = 0; i < s.maxUsed; ++i)
            if (s.alive[i] == 1 && s.type[i] == FE_L && s.tmp[i] > 100) s.tmp[i] -= 50;
        s.step();
    }
    const int molten2 = countOf(s, FE_L), solid2 = countOf(s, FE);
    ok(solid2 > solid, "расплав застыл обратно в железо",
       "твёрдого стало " + std::to_string(solid2) + ", расплава " + std::to_string(molten2));

    // Мерцание: вещество не должно скакать через точку перехода каждый
    // шаг. Держим ровно на точке плавления и считаем превращения.
    {
        World f(20, 20);
        f.rng.seed(3);
        for (int x = 0; x < 20; ++x) f.create(x, 19, ST);
        const int i0 = f.create(10, 18, FE);
        int flips = 0, prev = f.type[i0];
        for (int step = 0; step < 300; ++step) {
            for (int i = 0; i < f.maxUsed; ++i)
                if (f.alive[i] == 1 && (f.type[i] == FE || f.type[i] == FE_L))
                    f.tmp[i] = 1537.8;
            f.step();
            const int o = f.at(10, 18);
            if (o >= 0 && f.type[o] != prev) { ++flips; prev = f.type[o]; }
        }
        ok(flips <= 4, "на точке плавления вещество не мерцает",
           std::to_string(flips) + " превращений за 300 шагов");
    }

    // Тепло внутри тела. Брусок делаем толстым: у тонкого все частицы
    // на поверхности, и он честно остывает в воздух — на нём ничего не
    // измеришь. У толстого середина воздуха не касается.
    {
        World b(40, 20);
        b.rng.seed(1);
        for (int y = 6; y < 15; ++y)
            for (int x = 5; x < 35; ++x) b.create(x, y, FE);
        for (int i = 0; i < b.maxUsed; ++i) if (b.alive[i] == 1) b.tmp[i] = 20.0;
        for (int step = 1; step <= 4000; ++step) {
            for (int y = 6; y < 15; ++y) {
                const int hot = b.at(5, y);
                if (hot >= 0) b.tmp[hot] = 1000.0;
            }
            b.step();
        }
        const int mid = b.at(20, 10), far = b.at(33, 10);
        const double tm = (mid >= 0) ? b.tmp[mid] : -1;
        const double tf = (far >= 0) ? b.tmp[far] : -1;
        char buf[160];
        std::snprintf(buf, sizeof buf, "середина %.0f, дальний конец %.0f градусов", tm, tf);
        ok(tm > 250 && tf > 90, "тепло доходит до середины и до конца бруска", buf);

        // И не уходит в никуда: раньше к комнатной температуре тянулась
        // каждая частица, даже внутри тела. Дальний конец застревал
        // около 48 градусов и дальше не рос никогда.
        ok(tf > 100, "остывает поверхность, а не весь объём",
           std::to_string(int(tf)) + " градусов против 48 при остывании объёмом");
    }

    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
