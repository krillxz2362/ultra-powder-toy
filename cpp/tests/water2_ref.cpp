// ВОДА 2.0: три микропесочницы с котлом, одна сцена на всех.
// Главная проверка — объём воды не гуляет.
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

// Котёл в воде: чаша из металла, вокруг и внутри вода.
static void scene(World& s, int W, int H) {
    const int ST = ID("STONE"), ME = ID("METAL"), WA = ID("WATER");
    for (int x = 0; x < W; ++x) { s.create(x, H - 1, ST); s.create(x, H - 2, ST); }
    for (int y = H / 2; y < H - 2; ++y) { s.create(0, y, ST); s.create(W - 1, y, ST); }
    // котёл: стенки и дно
    const int cx0 = W / 3, cx1 = 2 * W / 3, cy0 = H / 2 + 4, cy1 = H - 6;
    for (int x = cx0; x <= cx1; ++x) s.create(x, cy1, ME);
    for (int y = cy0; y <= cy1; ++y) { s.create(cx0, y, ME); s.create(cx1, y, ME); }
    // вода внутри котла и снаружи до половины
    for (int y = cy0 + 1; y < cy1; ++y)
        for (int x = cx0 + 1; x < cx1; ++x) s.create(x, y, WA);
    for (int y = H - 12; y < H - 2; ++y)
        for (int x = 1; x < W - 1; ++x) if (s.at(x, y) < 0) s.create(x, y, WA);
}

static void run(const char* name, int model, int steps) {
    const int W = 120, H = 80;
    World s(W, H);
    s.rng.seed(2026);
    s.liquidModel = model;
    scene(s, W, H);
    if (model != World::LIQ_CELLS) s.fill.assign(s.maxp(), 1.0);

    const double v0 = s.liquidVolume();
    double worst = 0.0;
    for (int i = 0; i < steps; ++i) {
        s.step();
        const double v = s.liquidVolume();
        worst = std::max(worst, std::fabs(v - v0));
    }
    const double v1 = s.liquidVolume();
    char buf[160];
    std::snprintf(buf, sizeof buf,
        "было %.1f, стало %.1f, худшее расхождение %.3f, капель %zu",
        v0, v1, worst, s.drops.size());
    ok(worst < v0 * 0.02, (std::string("объём воды не гуляет: ") + name).c_str(), buf);
}

int main() {
    std::printf("=== три песочницы, одна сцена: котёл в воде ===\n");
    run("как есть (клетки)", World::LIQ_CELLS, 400);
    run("доля заполнения",   World::LIQ_FILL,  400);
    run("гибрид",            World::LIQ_HYBRID, 400);
    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
