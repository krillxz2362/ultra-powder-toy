// Проверка панели: раскладка, попадание по кнопкам, кисть.
#include "upt_core.h"
#include "upt_ui.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace upt;
static int fails = 0;
static void ok(bool c, const char* what, const char* got = "") {
    std::printf("%s %s %s\n", c ? "[ОК]  " : "[ПЛОХО]", what, got);
    if (!c) ++fails;
}
int main() {
    std::vector<int> favs; favs.push_back(0);
    for (int i = 1; i < SUBSTANCE_COUNT; ++i) { if (SUBSTANCES[i].z != 0) break; favs.push_back(i); }
    ok(favs.size() == 27, "в панели 27 избранных веществ");

    Ui ui; ui.layout(900, 688, favs);
    ok(ui.panelH() > 0 && ui.y0() > 0, "панель заняла низ экрана");
    ok(ui.buttons.size() == favs.size() + 1 + 8, "кнопок: вещества, ХИМИЯ и ряд управления");

    // Ни одна кнопка не вылезает за экран и они не наезжают друг на друга.
    bool inside = true, overlap = false;
    for (size_t i = 0; i < ui.buttons.size(); ++i) {
        const Button& a = ui.buttons[i];
        if (a.x < 0 || a.y < 0 || a.x + a.w > 900 || a.y + a.h > 688) inside = false;
        for (size_t j = i + 1; j < ui.buttons.size(); ++j) {
            const Button& b = ui.buttons[j];
            if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h)
                overlap = true;
        }
    }
    ok(inside, "все кнопки внутри экрана");
    ok(!overlap, "кнопки не наезжают друг на друга");

    // Попадание в середину каждой кнопки возвращает её же.
    bool hitOk = true;
    for (const Button& b : ui.buttons) {
        const Button* g = ui.hit(b.x + b.w / 2, b.y + b.h / 2);
        if (!g || g->act != b.act || g->id != b.id) hitOk = false;
    }
    ok(hitOk, "нажатие попадает в свою кнопку");
    ok(ui.hit(10, 10) == nullptr, "над миром кнопок нет");

    // Кисть кладёт круг и ластик его убирает.
    World w(80, 60);
    int SAND = 0;
    for (int i = 0; i < SUBSTANCE_COUNT; ++i) if (std::strcmp(SUBSTANCES[i].key, "SAND") == 0) SAND = i;
    int r = 5, put = 0;
    for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx)
        if (dx*dx + dy*dy <= r*r && w.create(40 + dx, 30 + dy, SAND) >= 0) ++put;
    ok(put > 70 && put < 90, "кисть радиуса 5 кладёт около 80 частиц",
       (std::to_string(put) + " шт.").c_str());
    int seen = 0;
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 80; ++x) if (w.at(x, y) >= 0) ++seen;
    ok(seen == put, "всё легло в мир");
    for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx)
        if (dx*dx + dy*dy <= r*r) w.killAt(40 + dx, 30 + dy);
    seen = 0;
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 80; ++x) if (w.at(x, y) >= 0) ++seen;
    ok(seen == 0, "ластик убрал всё");

    // Очистка мира.
    for (int x = 0; x < 80; ++x) w.create(x, 59, SAND);
    w.clearWorld();
    seen = 0;
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 80; ++x) if (w.at(x, y) >= 0) ++seen;
    ok(seen == 0, "ОЧИСТИТЬ убирает мир целиком");

    // Шрифт: русские буквы считаются по одной, а не по байтам.
    ok(textWidth("ПАУЗА", 2) == 5 * 12 - 2, "ширина русской строки верна");
    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
