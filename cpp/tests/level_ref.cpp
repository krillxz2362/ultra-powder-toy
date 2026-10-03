// Выравнивание жидкости: горб над бугром, сообщающиеся сосуды, покой.
// Три опыта, которые ловят разные беды: неумение растекаться вбок,
// неумение перетекать между коленами и самопроизвольное бурление.
#include "upt_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace upt;
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::string(SUBSTANCES[i].key) == k) return i;
    return -1;
}

// Поверхность воды: средний уровень, неровность, перепад края-середина.
struct Surf { double level, rough; int lo, hi, n; };
static Surf surface(World& s, int WA) {
    double m = 0, m2 = 0; int c = 0, lo = 1 << 30, hi = -1;
    for (int x = 1; x < s.width() - 1; ++x)
        for (int y = 0; y < s.height(); ++y) {
            const int o = s.at(x, y);
            if (o >= 0 && s.type[o] == WA) {
                m += y; m2 += double(y) * y; ++c;
                if (y < lo) lo = y;
                if (y > hi) hi = y;
                break;
            }
        }
    if (!c) return {0, 0, 0, 0, 0};
    m /= c;
    return {m, std::sqrt(m2 / c - m * m), lo, hi, c};
}

static void dome(int steps) {
    const int W = 160, H = 90, ST = ID("STONE"), WA = ID("WATER");
    World s(W, H); s.rng.seed(4242);
    for (int x = 0; x < W; ++x) { s.create(x, H - 1, ST); s.create(x, H - 2, ST); }
    for (int y = 40; y < H - 2; ++y) { s.create(0, y, ST); s.create(W - 1, y, ST); }
    for (int dx = -45; dx <= 45; ++dx) {               // бугор на дне
        const int hgt = int(26 - std::fabs(dx) * 0.55);
        for (int k = 0; k < hgt; ++k) s.create(W / 2 + dx, H - 3 - k, ST);
    }
    for (int y = 46; y < H - 2; ++y) for (int x = 1; x < W - 1; ++x) s.create(x, y, WA);
    for (int i = 0; i < steps; ++i) s.step();
    const Surf f = surface(s, WA);
    std::printf("горб над бугром: уровень %6.2f  неровность %5.3f  перепад %2d клеток\n",
                f.level, f.rough, f.hi - f.lo);
}

static void vessels(int steps) {
    const int W = 80, H = 50, ST = ID("STONE"), WA = ID("WATER");
    World s(W, H); s.rng.seed(777);
    for (int x = 0; x < W; ++x) s.create(x, H - 1, ST);
    for (int y = 20; y <= H - 2; ++y) { s.create(0, y, ST); s.create(W - 1, y, ST); }
    for (int y = 20; y <= H - 12; ++y) s.create(40, y, ST);   // перемычка до низа
    for (int y = 21; y <= H - 2; ++y) for (int x = 1; x <= 39; ++x) s.create(x, y, WA);
    for (int i = 0; i < steps; ++i) s.step();
    int l = 0, r = 0;
    for (int k = 0; k < s.maxUsed; ++k)
        if (s.alive[k] == 1 && s.type[k] == WA) { if (s.px[k] < 40) ++l; else ++r; }
    auto top = [&](int x) {
        for (int y = 0; y < H; ++y) { const int o = s.at(x, y);
            if (o >= 0 && s.type[o] == WA) return y; }
        return -1;
    };
    std::printf("сосуды: слева %4d справа %4d (поровну 546)  уровни %2d и %2d\n",
                l, r, top(10), top(70));
}

static void calm(int steps) {
    const int W = 80, H = 50, ST = ID("STONE"), WA = ID("WATER");
    World s(W, H); s.rng.seed(123);
    for (int x = 0; x < W; ++x) s.create(x, H - 1, ST);
    for (int y = 20; y <= H - 2; ++y) { s.create(0, y, ST); s.create(W - 1, y, ST); }
    for (int y = 30; y <= H - 2; ++y) for (int x = 1; x <= W - 2; ++x) s.create(x, y, WA);
    for (int i = 0; i < steps; ++i) s.step();
    const Surf f = surface(s, WA);
    int aw = 0;
    for (size_t i = 0; i < s.awake.size(); ++i) if (s.awake[i]) ++aw;
    std::printf("покой: уровень %6.2f  неровность %5.3f  бодрых кусков %d\n",
                f.level, f.rough, aw);
}

int main(int argc, char** argv) {
    const int steps = (argc > 1) ? std::atoi(argv[1]) : 2000;
    std::printf("шагов: %d\n", steps);
    dome(steps); vessels(steps); calm(steps);
}
