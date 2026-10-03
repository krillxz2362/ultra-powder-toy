// Тела: верёвка, кукла, вода, песок.
#include "upt_body.h"
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

int main() {
    const int ST = ID("STONE"), WATER = ID("WATER"), SAND = ID("SAND");

    // 1. Верёвка висит и не растягивается.
    {
        World w(60, 60);
        Bodies b;
        b.makeRope(30, 5, 10, 1.2);
        for (int i = 0; i < 600; ++i) b.step(w);
        const double len = std::hypot(b.pts[0].x - b.pts[9].x, b.pts[0].y - b.pts[9].y);
        char buf[80];
        std::snprintf(buf, sizeof buf, "длина %.2f при должной %.2f", len, 9 * 1.2);
        ok(len > 9 * 1.2 * 0.9 && len < 9 * 1.2 * 1.15, "верёвка висит и не тянется", buf);
        ok(b.pts[0].y < 6.0, "верхняя точка осталась прибитой");
    }

    // 2. Точка не проваливается сквозь каменный пол.
    {
        World w(60, 60);
        for (int x = 0; x < 60; ++x) for (int y = 50; y < 60; ++y) w.create(x, y, ST);
        Bodies b;
        const int p = b.addPoint(30, 5, 0.6, 1.0, false, 1);
        for (int i = 0; i < 900; ++i) b.step(w);
        char buf[80];
        std::snprintf(buf, sizeof buf, "остановилась на %.2f, пол на 50", b.pts[p].y);
        ok(b.pts[p].y < 50.5 && b.pts[p].y > 47.0, "точка легла на пол и не провалилась", buf);
    }

    // 3. Кукла падает со стола и остаётся целой.
    {
        World w(80, 60);
        for (int x = 0; x < 80; ++x) for (int y = 52; y < 60; ++y) w.create(x, y, ST);
        Bodies b;
        const int doll = b.makeDoll(40, 10, 1.4);
        double sum0 = 0;
        for (const Link& l : b.links) sum0 += l.rest;
        for (int i = 0; i < 1200; ++i) b.step(w);
        double sum1 = 0;
        bool sane = true;
        for (const Link& l : b.links) {
            const double d = std::hypot(b.pts[l.a].x - b.pts[l.b].x,
                                        b.pts[l.a].y - b.pts[l.b].y);
            sum1 += d;
            if (d > l.rest * 2.0 + 1.0) sane = false;
            if (!std::isfinite(d)) sane = false;
        }
        char buf[100];
        std::snprintf(buf, sizeof buf, "сумма связей %.1f против %.1f", sum1, sum0);
        ok(sane, "кукла после падения цела, связи не разорваны", buf);
        double lo = 1e9;
        for (const BodyPoint& p : b.pts) if (p.body == doll) lo = std::min(lo, p.y);
        ok(lo > 40.0, "кукла долетела до пола", "верхняя точка на " + std::to_string(int(lo)));
        bool below = false;
        for (const BodyPoint& p : b.pts) if (p.y > 53.5) below = true;
        ok(!below, "ни одна точка не ушла под пол");
    }

    // 4. Кукла плавает в воде, а не тонет камнем.
    {
        World w(80, 60);
        for (int x = 0; x < 80; ++x) w.create(x, 59, ST);
        for (int y = 30; y < 59; ++y) for (int x = 1; x < 79; ++x) w.create(x, y, WATER);
        Bodies b;
        const int doll = b.makeDoll(40, 10, 1.4);
        for (int i = 0; i < 1500; ++i) { w.step(); b.step(w); }
        double deep = -1;
        for (const BodyPoint& p : b.pts) if (p.body == doll) deep = std::max(deep, p.y);
        char buf[90];
        std::snprintf(buf, sizeof buf, "нижняя точка на %.1f (вода с 30, дно 59)", deep);
        // Человек чуть легче воды и всплывает к поверхности, а не
        // зависает там, куда его занесло падением.
        ok(deep < 42.0, "кукла всплывает к поверхности", buf);
    }

    // 5. Кукла расталкивает песок, а не стоит на нём статуей.
    {
        World w(80, 60);
        for (int x = 0; x < 80; ++x) w.create(x, 59, ST);
        int before = 0;
        for (int y = 44; y < 59; ++y) for (int x = 20; x < 60; ++x)
            if (w.create(x, y, SAND) >= 0) ++before;
        Bodies b;
        b.makeDoll(40, 20, 1.4);
        for (int i = 0; i < 800; ++i) { w.step(); b.step(w); }
        // Кукла легче песка и обязана лежать НА куче, а не внутри.
        // Считать занятые клетки бессмысленно: лёжа на песке, точка
        // всё равно соприкасается с ним.
        int surface = 60;
        for (int y = 0; y < 60; ++y) {
            bool any = false;
            for (int x = 20; x < 60; ++x) { const int o = w.at(x, y);
                if (o >= 0 && w.type[o] == SAND) { any = true; break; } }
            if (any) { surface = y; break; }
        }
        double deepest = -1;
        for (const BodyPoint& p : b.pts) deepest = std::max(deepest, p.y);
        char sb[110];
        std::snprintf(sb, sizeof sb, "поверхность песка %d, нижняя точка %.1f",
                      surface, deepest);
        ok(deepest < surface + 4.0, "кукла лежит на песке, а не тонет в нём", sb);
    }

    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
