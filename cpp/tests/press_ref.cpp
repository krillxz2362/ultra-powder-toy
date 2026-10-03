// Давление: держит ли стена в один пиксель.
//
// В The Powder Toy это известный изъян: воздух считается по клеткам
// 4x4, и перегородка тоньше клетки давление не держит — газ уходит
// сквозь неё. Мы договорились, что у нас держать должна.
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
    const int W = 120, H = 90;
    // Пар, а не горючий газ: газ при нагреве до 600 градусов
    // прореагирует и исчезнет — опыт будет мерить не то.
    const int ST = ID("STONE"), GAS = ID("STEAM"), WATER = ID("WATER");

    // Коробка со стенками в одну клетку, внутри газ. Снаружи пусто.
    World s(W, H);
    s.rng.seed(2024);
    const int x0 = 30, x1 = 80, y0 = 25, y1 = 70;
    for (int x = x0; x <= x1; ++x) { s.create(x, y0, ST); s.create(x, y1, ST); }
    for (int y = y0; y <= y1; ++y) { s.create(x0, y, ST); s.create(x1, y, ST); }
    int inside0 = 0;
    for (int y = y0 + 1; y < y1; ++y)
        for (int x = x0 + 1; x < x1; ++x)
            if (s.create(x, y, GAS) >= 0) ++inside0;

    // Греем газ: давление в коробке должно вырасти, а газ — остаться.
    for (int i = 0; i < 600; ++i) {
        for (int y = y0 + 1; y < y1; ++y)
            for (int x = x0 + 1; x < x1; ++x) {
                const int o = s.at(x, y);
                if (o >= 0 && s.type[o] == GAS) s.tmp[o] = 300.0;
            }
        s.step();
    }

    int in = 0, out = 0;
    for (int i = 0; i < s.maxUsed; ++i) {
        if (s.alive[i] != 1 || s.type[i] != GAS) continue;
        const int x = int(s.px[i]), y = int(s.py[i]);
        if (x > x0 && x < x1 && y > y0 && y < y1) ++in; else ++out;
    }
    ok(inside0 > 1500, "коробка наполнилась газом", std::to_string(inside0) + " частиц");
    ok(out == 0, "горячий газ не прошёл сквозь стену в одну клетку",
       "наружу ушло " + std::to_string(out));
    ok(in > inside0 * 0.9, "газ остался внутри",
       std::to_string(in) + " из " + std::to_string(inside0));

    // Давление внутри должно быть заметно выше, чем снаружи.
    double pIn = 0, pOut = 0; int nIn = 0, nOut = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double p = s.air.pv[s.air.index(x, y)];
            if (x > x0 + 2 && x < x1 - 2 && y > y0 + 2 && y < y1 - 2) { pIn += p; ++nIn; }
            else if (x < x0 - 4 || x > x1 + 4) { pOut += p; ++nOut; }
        }
    pIn /= nIn; pOut /= nOut;
    char buf[120];
    std::snprintf(buf, sizeof buf, "внутри %.3f, снаружи %.3f", pIn, pOut);
    ok(pIn > pOut + 0.05, "давление внутри выше, чем снаружи", buf);

    // Вода в коробке с дыркой в одну клетку: должна вытечь. Стена
    // держит, а дырка — нет, иначе это уже не физика, а броня.
    World d(W, H);
    d.rng.seed(7);
    for (int x = x0; x <= x1; ++x) { d.create(x, y0, ST); d.create(x, y1, ST); }
    for (int y = y0; y <= y1; ++y) { d.create(x0, y, ST); d.create(x1, y, ST); }
    for (int x = 0; x < W; ++x) d.create(x, H - 1, ST);
    d.killAt(x0, y1 - 1);                       // дырка в одну клетку сбоку
    int poured = 0;
    for (int y = y0 + 1; y < y1; ++y)
        for (int x = x0 + 1; x < x1; ++x)
            if (d.create(x, y, WATER) >= 0) ++poured;
    for (int i = 0; i < 1500; ++i) d.step();
    int left = 0;
    for (int i = 0; i < d.maxUsed; ++i) {
        if (d.alive[i] != 1 || d.type[i] != WATER) continue;
        const int x = int(d.px[i]), y = int(d.py[i]);
        if (x > x0 && x < x1 && y > y0 && y < y1) ++left;
    }
    // Предел клеточного мира: через дырку шириной в одну клетку не
    // пройдёт больше одной частицы за шаг, как ни дави. По Торричелли
    // при глубине 43 клетки скорость струи 3.5 клетки за шаг, но
    // пропустить столько клетка не может — упираемся в саму сетку.
    // Сейчас выходит 0.39 за шаг: частица задерживается в проёме на
    // пару шагов. Цель — приблизиться к 1.0.
    const double rate = double(poured - left) / 1500.0;
    char rbuf[120];
    std::snprintf(rbuf, sizeof rbuf, "%.2f частицы за шаг (предел клетки 1.0)", rate);
    ok(rate > 0.25, "сквозь дырку в одну клетку вода течёт не иссякая", rbuf);
    ok(left < poured, "воды в коробке убывает",
       "осталось " + std::to_string(left) + " из " + std::to_string(poured));

    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
