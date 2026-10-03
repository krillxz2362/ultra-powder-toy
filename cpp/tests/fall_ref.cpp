// Падение пласта порошка. По закону Ньютона все песчинки в свободном
// падении имеют одно ускорение, значит пласт обязан лететь целым:
// его высота не должна меняться, а путь — совпадать с 0.5*g*t^2.
#include "upt_core.h"
#include <cstdio>
#include <string>
#include <cmath>
using namespace upt;
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::string(SUBSTANCES[i].key) == k) return i;
    return -1;
}
int main() {
    const int W = 200, H = 400;
    const int SAND = ID("SAND"), STONE = ID("STONE");
    World s(W, H); s.rng.seed(11);
    for (int x = 0; x < W; ++x) s.create(x, H-1, STONE);
    for (int y = 10; y < 40; ++y) for (int x = 75; x < 125; ++x) s.create(x, y, SAND);

    std::printf("%5s %9s %9s %9s %12s\n", "шаг", "верх", "низ", "высота", "разброс вбок");
    for (int step = 0; step <= 120; ++step) {
        if (step % 20 == 0) {
            double sx = 0, ymin = 1e9, ymax = -1e9; int n = 0;
            for (int i = 0; i < s.maxUsed; ++i) if (s.alive[i] == 1 && s.type[i] == SAND) {
                sx += s.px[i]; ++n;
                if (s.py[i] < ymin) ymin = s.py[i];
                if (s.py[i] > ymax) ymax = s.py[i];
            }
            const double cx = sx / n;
            double dev = 0;
            for (int i = 0; i < s.maxUsed; ++i) if (s.alive[i] == 1 && s.type[i] == SAND)
                dev += std::fabs(s.px[i] - cx);
            std::printf("%5d %9.2f %9.2f %9.2f %12.2f\n", step, ymin, ymax, ymax - ymin, dev / n);
        }
        s.step();
    }
}
