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
    ok(ui.buttons.size() == favs.size() + 1 + 10,
       "кнопок: вещества, ХИМИЯ и ряд управления",
       (std::to_string(ui.buttons.size()) + " шт.").c_str());

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

    // Издания и ветка администратора.
    {
        Ui base; base.chemAvailable = false; base.layout(900, 688, favs);
        bool hasChem = false;
        for (const Button& b : base.buttons) if (b.act == Act::Chem) hasChem = true;
        ok(!hasChem, "в издании Base ячейки ХИМИЯ нет");
        ok(base.buttons.size() == favs.size() + 10,
           "в Base кнопок на одну меньше",
           (std::to_string(base.buttons.size()) + " шт.").c_str());

        Ui adm; adm.admin = true; adm.layout(900, 688, favs);
        bool hasMgr = false;
        for (const Button& b : adm.buttons) if (b.act == Act::MgrOpen) hasMgr = true;
        ok(hasMgr, "в ветке UptA есть кнопка списка сохранений");

        // Менеджер: строка на сохранение плюс крестик, и кнопка закрытия.
        adm.layoutManager(900, 688, 3);
        int rows = 0, dels = 0, closes = 0;
        for (const Button& b : adm.mgrButtons) {
            if (b.act == Act::MgrLoad) ++rows;
            else if (b.act == Act::MgrDelete) ++dels;
            else if (b.act == Act::MgrClose) ++closes;
        }
        ok(rows == 3 && dels == 3 && closes == 1,
           "менеджер разложил три сохранения",
           (std::to_string(rows) + " строк").c_str());
        bool mgrIn = true, mgrOver = false;
        for (size_t i = 0; i < adm.mgrButtons.size(); ++i) {
            const Button& a = adm.mgrButtons[i];
            if (a.x < 0 || a.y < 0 || a.x + a.w > 900 || a.y + a.h > 688) mgrIn = false;
            for (size_t j = i + 1; j < adm.mgrButtons.size(); ++j) {
                const Button& b = adm.mgrButtons[j];
                if (a.x < b.x + b.w && b.x < a.x + a.w &&
                    a.y < b.y + b.h && b.y < a.y + a.h) mgrOver = true;
            }
        }
        ok(mgrIn && !mgrOver, "строки менеджера не вылезают и не наезжают");
        // Сто сохранений не должны вылезти за экран.
        adm.layoutManager(900, 688, 100);
        bool fits = true;
        for (const Button& b : adm.mgrButtons)
            if (b.y + b.h > 688) fits = false;
        ok(fits, "лишние сохранения просто не рисуются");
    }

    // Экран ХИМИЯ.
    ui.layoutChem(900, 688);
    ok(ui.chemButtons.size() == 119, "в таблице 118 элементов и кнопка закрытия",
       (std::to_string(ui.chemButtons.size()) + " кнопок").c_str());
    bool cIn = true, cOver = false;
    for (size_t i = 0; i < ui.chemButtons.size(); ++i) {
        const Button& a = ui.chemButtons[i];
        if (a.x < 0 || a.y < 0 || a.x + a.w > 900 || a.y + a.h > 688) cIn = false;
        for (size_t j = i + 1; j < ui.chemButtons.size(); ++j) {
            const Button& b = ui.chemButtons[j];
            if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h)
                cOver = true;
        }
    }
    ok(cIn, "таблица помещается в экран");
    ok(!cOver, "клетки таблицы не наезжают друг на друга");
    bool ids = true;
    for (const Button& b : ui.chemButtons)
        if (b.act == Act::ChemPick && (b.id <= 0 || b.id >= SUBSTANCE_COUNT)) ids = false;
    ok(ids, "у каждой клетки таблицы есть своё вещество");
    {
        const Button* b = ui.hitChem(ui.chemButtons[5].x + 2, ui.chemButtons[5].y + 2);
        ok(b != nullptr && b->act == Act::ChemPick, "нажатие по таблице попадает в элемент");
    }

    // Прямоугольник мира: картинка, стрелки и палец обязаны считаться
    // по нему одинаково. Именно это разъезжалось на телефоне.
    {
        bool allOk = true;
        // разные экраны, в том числе «неудобные» размеры
        const int cases[][2] = {{900, 620}, {1568, 688}, {2340, 1080}, {720, 1480}};
        for (const auto& c2 : cases) {
            const int sw = c2[0], sh = c2[1];
            Ui u2; u2.layout(sw, sh, favs);
            int sc = std::max(2, std::min(6, sw / 420));
            WorldView wv;
            wv.x = 0; wv.y = 0; wv.w = sw; wv.h = u2.y0();
            wv.cols = std::max(40, sw / sc);
            wv.rows = std::max(40, u2.y0() / sc);
            // Левый верхний угол мира — клетка 0,0; правый нижний — последняя.
            if (wv.cellX(0) != 0 || wv.cellY(0) != 0) allOk = false;
            if (wv.cellX(wv.w - 1) != wv.cols - 1) allOk = false;
            if (wv.cellY(wv.h - 1) != wv.rows - 1) allOk = false;
            // Ни одна точка над панелью не выпадает за мир.
            for (int px = 0; px < wv.w; px += 7) {
                const int cx = wv.cellX(px);
                if (cx < 0 || cx >= wv.cols) { allOk = false; break; }
            }
            for (int py = 0; py < wv.h; py += 7) {
                const int cy = wv.cellY(py);
                if (cy < 0 || cy >= wv.rows) { allOk = false; break; }
            }
            // Мир кончается ровно там, где начинается панель.
            if (wv.y + wv.h != u2.y0()) allOk = false;
        }
        ok(allOk, "мир, стрелки и палец считаются по одному прямоугольнику");
    }

    // Шрифт: русские буквы считаются по одной, а не по байтам.
    ok(textWidth("ПАУЗА", 2) == 5 * 12 - 2, "ширина русской строки верна");
    ok(textWidth("He", 2) == textWidth("HE", 2), "строчные считаются как заглавные");
    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
