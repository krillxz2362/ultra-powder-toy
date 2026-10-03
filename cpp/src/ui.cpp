#include "upt_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "upt_core.h"
#include "upt_chem.h"
#include "upt_font.h"

namespace upt {

// ---------- холст ----------

void Canvas::dot(int x, int y, Color c, uint8_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a == 0) return;
    uint8_t* p = px + (static_cast<size_t>(y) * w + x) * 4;
    if (a == 255) {
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
        return;
    }
    const int ia = 255 - a;
    p[0] = static_cast<uint8_t>((c.r * a + p[0] * ia) / 255);
    p[1] = static_cast<uint8_t>((c.g * a + p[1] * ia) / 255);
    p[2] = static_cast<uint8_t>((c.b * a + p[2] * ia) / 255);
    p[3] = 255;
}

void Canvas::fill(int x, int y, int ww, int hh, Color c, uint8_t a) {
    const int x1 = std::min(x + ww, w), y1 = std::min(y + hh, h);
    for (int yy = std::max(0, y); yy < y1; ++yy)
        for (int xx = std::max(0, x); xx < x1; ++xx) dot(xx, yy, c, a);
}

void Canvas::frame(int x, int y, int ww, int hh, Color c, uint8_t a) {
    fill(x, y, ww, 1, c, a);
    fill(x, y + hh - 1, ww, 1, c, a);
    fill(x, y, 1, hh, c, a);
    fill(x + ww - 1, y, 1, hh, c, a);
}

// ---------- шрифт ----------

// Разбор UTF-8: буквы кириллицы занимают два байта, и без разбора
// панель показывала бы кракозябры.
static std::vector<uint32_t> decodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int extra = 0;
        if (c >= 0xF0)      { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        while (extra-- > 0 && i < s.size()) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
            ++i;
        }
        out.push_back(cp);
    }
    return out;
}

// Строчных букв в шрифте нет: как и в эталоне, приводим их к заглавным.
// Без этого от «He» рисовалась одна «H», и таблица элементов читалась
// как набор отдельных букв.
static uint32_t upperCp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 'a' + 'A';
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x430 + 0x410;   // а-я → А-Я
    if (cp == 0x451) return 0x401;                               // ё → Ё
    return cp;
}

static const Glyph* findGlyph(uint32_t cp) {
    int lo = 0, hi = GLYPH_COUNT - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (GLYPHS[mid].cp == cp) return &GLYPHS[mid];
        if (GLYPHS[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

int textWidth(const std::string& s, int scale) {
    const auto cps = decodeUtf8(s);
    if (cps.empty()) return 0;
    return static_cast<int>(cps.size()) * 6 * scale - scale;
}

void drawText(Canvas& cv, int x, int y, const std::string& s, int scale, Color c) {
    const auto cps = decodeUtf8(s);
    int cx = x;
    for (uint32_t cp : cps) {
        if (cp == ' ') { cx += 6 * scale; continue; }
        const Glyph* g = findGlyph(upperCp(cp));
        if (g) {
            for (int row = 0; row < 7; ++row) {
                const uint8_t bits = g->rows[row];
                for (int col = 0; col < 5; ++col) {
                    if (!(bits & (1 << (4 - col)))) continue;
                    cv.fill(cx + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        cx += 6 * scale;
    }
}

// Масштаб, при котором подпись влезает в ячейку.
static int fitScale(const std::string& s, int boxW, int maxScale) {
    const int one = textWidth(s, 1);
    if (one <= 0) return 1;
    int sc = (boxW - 6) / one;
    if (sc < 1) sc = 1;
    if (sc > maxScale) sc = maxScale;
    return sc;
}

// ---------- панель ----------

void Ui::layout(int screenW, int screenH, const std::vector<int>& favourites) {
    screenW_ = screenW; screenH_ = screenH;
    const int favN = static_cast<int>(favourites.size());
    const int rows = (favN + 1 + cols_ - 1) / cols_;

    // Высота ряда от экрана. Потолок в 46 точек годился для окна на
    // столе, но на телефоне с плотным экраном это четыре миллиметра —
    // пальцем не попасть. Ряд должен быть около сантиметра.
    rowH_ = static_cast<int>(screenH * 0.052);
    rowH_ = std::clamp(rowH_, 28, 120);
    ctrlH_ = rowH_ - 2;
    // Два ряда управления: во втором — температура кисти. В один ряд
    // её не втиснуть, кнопки стали бы уже пальца.
    panelH_ = ctrlH_ * 2 + rows * rowH_;
    y0_ = screenH - panelH_;

    buttons.clear();
    const double colW = static_cast<double>(screenW) / cols_;

    for (int i = 0; i < favN; ++i) {
        const int id = favourites[i];
        const Substance& S = SUBSTANCES[id];
        const int c = i % cols_, r = i / cols_;
        Button b;
        b.x = static_cast<int>(c * colW);
        b.y = y0_ + ctrlH_ * 2 + r * rowH_;
        b.w = static_cast<int>((c + 1) * colW) - b.x;
        b.h = rowH_;
        b.act = Act::Material;
        b.id = id;
        b.label = S.name;
        b.color = Color{static_cast<uint8_t>(S.r), static_cast<uint8_t>(S.g),
                        static_cast<uint8_t>(S.b)};
        buttons.push_back(b);
    }
    if (chemAvailable) {   // ячейка выхода в таблицу химии
        const int c = favN % cols_, r = favN / cols_;
        Button b;
        b.x = static_cast<int>(c * colW);
        b.y = y0_ + ctrlH_ * 2 + r * rowH_;
        b.w = static_cast<int>((c + 1) * colW) - b.x;
        b.h = rowH_;
        b.act = Act::Chem;
        b.label = "ХИМИЯ";
        b.color = Color{60, 70, 90};
        buttons.push_back(b);
    }

    // Ряд управления. Веса те же, что в LÖVE: подписи разной длины, и
    // равные доли резали бы «ОЧИСТИТЬ».
    const char* namesBase[] = {"ПАУЗА", "ШАГ", "<", "КИСТЬ", ">", "ВИД", "ПОТОК",
                               "СОХР", "ЗАГР", "ОЧИСТИТЬ"};
    const Act actsBase[] = {Act::Pause, Act::Step, Act::BrushDown, Act::None,
                            Act::BrushUp, Act::View, Act::Arrows,
                            Act::Save, Act::Load, Act::Clear};
    const double wqBase[] = {1.0, 0.65, 0.45, 0.75, 0.45, 0.8, 0.85, 0.8, 0.8, 1.15};

    // В ветке администратора добавляется список сохранений.
    const char* namesAdm[] = {"ПАУЗА", "ШАГ", "<", "КИСТЬ", ">", "ВИД", "ПОТОК",
                              "СОХР", "ЗАГР", "СПИСОК", "ОЧИСТИТЬ"};
    const Act actsAdm[] = {Act::Pause, Act::Step, Act::BrushDown, Act::None,
                           Act::BrushUp, Act::View, Act::Arrows,
                           Act::Save, Act::Load, Act::MgrOpen, Act::Clear};
    const double wqAdm[] = {0.95, 0.6, 0.42, 0.7, 0.42, 0.75, 0.8, 0.75, 0.75, 0.95, 1.05};

    // Второй ряд: температура кисти.
    {
        const char* tn[] = {"T-", "ТЕМП", "T+", "АВТО"};
        const Act ta[] = {Act::TempDown, Act::None, Act::TempUp, Act::TempAuto};
        const double tw[] = {0.6, 1.6, 0.6, 0.8};
        double tot = 0;
        for (double q : tw) tot += q;
        double cx2 = 0;
        for (int i = 0; i < 4; ++i) {
            const double ww = screenW * tw[i] / tot;
            Button b;
            b.x = static_cast<int>(cx2);
            b.y = y0_ + ctrlH_;
            b.w = static_cast<int>(cx2 + ww) - b.x;
            b.h = ctrlH_;
            b.act = ta[i];
            b.label = tn[i];
            b.color = Color{40, 44, 54};
            buttons.push_back(b);
            cx2 += ww;
        }
    }

    const char** names = admin ? namesAdm : namesBase;
    const Act*   acts  = admin ? actsAdm  : actsBase;
    const double* wq   = admin ? wqAdm    : wqBase;
    const int n = admin ? 11 : 10;
    double total = 0;
    for (int i = 0; i < n; ++i) total += wq[i];
    double cx = 0;
    for (int i = 0; i < n; ++i) {
        const double ww = screenW * wq[i] / total;
        Button b;
        b.x = static_cast<int>(cx);
        b.y = y0_;
        b.w = static_cast<int>(cx + ww) - b.x;
        b.h = ctrlH_;
        b.act = acts[i];
        b.label = names[i];
        b.color = Color{44, 48, 58};
        buttons.push_back(b);
        cx += ww;
    }
}

const Button* Ui::hit(int x, int y) const {
    for (const Button& b : buttons)
        if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return &b;
    return nullptr;
}

// Светлая или тёмная подпись — по яркости плитки, иначе на жёлтом песке
// белые буквы пропадают.
static Color labelColor(Color bg) {
    const int lum = (bg.r * 299 + bg.g * 587 + bg.b * 114) / 1000;
    return lum > 140 ? Color{16, 16, 20} : Color{228, 230, 236};
}

void Ui::draw(Canvas& cv) const {
    const Color bg{26, 28, 34};
    cv.fill(0, y0_, screenW_, panelH_, bg);
    cv.fill(0, y0_, screenW_, 1, Color{70, 74, 86});

    for (const Button& b : buttons) {
        bool on = false;
        if (b.act == Act::Material) on = (b.id == selected);
        else if (b.act == Act::Pause) on = paused;
        else if (b.act == Act::Arrows) on = arrows;
        else if (b.act == Act::TempAuto) on = tempAuto;

        Color face = b.color;
        if (b.act == Act::Material) {
            // плитка вещества — его же цветом, чуть приглушённым
            face = Color{static_cast<uint8_t>(face.r * 0.75),
                         static_cast<uint8_t>(face.g * 0.75),
                         static_cast<uint8_t>(face.b * 0.75)};
            if (on) face = b.color;
        } else if (on) {
            face = Color{70, 96, 140};
        }
        cv.fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, face);
        cv.frame(b.x, b.y, b.w, b.h, on ? Color{235, 235, 240} : Color{54, 58, 70});

        std::string text = b.label;
        // Ячейка ХИМИЯ показывает то, что выбрано в таблице: иначе
        // непонятно, чем рисуешь, когда вещество не из избранных.
        if (b.act == Act::Chem) {
            bool fav = false;
            for (const Button& q : buttons)
                if (q.act == Act::Material && q.id == selected) { fav = true; break; }
            if (!fav && selected > 0) {
                text = SUBSTANCES[selected].name;
                face = Color{SUBSTANCES[selected].r, SUBSTANCES[selected].g,
                             SUBSTANCES[selected].b};
                cv.fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, face);
                cv.frame(b.x, b.y, b.w, b.h, Color{235, 235, 240});
            }
        }
        if (b.act == Act::None) {
            if (b.label == "ТЕМП") {
                if (tempAuto) {
                    text = "ТЕМП АВТО";
                } else {
                    char tb[48];
                    std::snprintf(tb, sizeof tb, "ТЕМП %d", static_cast<int>(brushTemp));
                    text = tb;
                }
            } else {
                text = "КИСТЬ " + std::to_string(brush);
            }
        }
        if (b.act == Act::View) {
            static const char* vn[] = {"ВИД", "ТЕПЛО", "ДАВЛ", "КИСЛ"};
            text = vn[view & 3];
        }
        const int sc = fitScale(text, b.w, b.h >= 36 ? 3 : 2);
        const int tw = textWidth(text, sc), th = 7 * sc;
        drawText(cv, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2, text, sc,
                 labelColor(face));
    }

    if (!status.empty()) {
        const int sc = 2;
        const int tw = textWidth(status, sc);
        cv.fill(4, 4, tw + 8, 7 * sc + 8, Color{12, 12, 16}, 180);
        drawText(cv, 8, 8, status, sc, Color{210, 214, 222});
    }
}

// ---------- стрелки потока ----------

// Отрезок по Брезенхэму: рисуем сами, чтобы стрелки ложились в тот же
// буфер, что и панель, — одним куском в видеопамять.
static void line(Canvas& cv, int x0, int y0, int x1, int y1, Color c, uint8_t a) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        cv.dot(x0, y0, c, a);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void drawFlow(Canvas& cv, const Air& air, const WorldView& wv) {
    // Размер ячейки воздуха на экране берём из того же прямоугольника,
    // в котором нарисован мир: стрелки обязаны лежать ровно на нём.
    const double cellX = AIR_CELL * wv.sx();
    const double cellY = AIR_CELL * wv.sy();
    int stepc = 1;
    while (cellX * stepc < 18.0) ++stepc;       // чаще, чем раз в 18 точек, незачем
    const double halfX = cellX * stepc * 0.5;
    const double halfY = cellY * stepc * 0.5;
    const double half = (halfX < halfY) ? halfX : halfY;

    for (int cy = 0; cy < air.chh; cy += stepc) {
        for (int cx = 0; cx < air.cw; cx += stepc) {
            const size_t i = static_cast<size_t>(cy) * air.cw + cx;
            const double ux = air.vx[i], uy = air.vy[i];
            const double sp = std::sqrt(ux * ux + uy * uy);
            if (sp <= 0.015) continue;          // штиль не рисуем

            const double px = wv.x + (cx * AIR_CELL) * wv.sx() + halfX;
            const double py = wv.y + (cy * AIR_CELL) * wv.sy() + halfY;
            if (px < wv.x || py < wv.y ||
                px > wv.x + wv.w || py > wv.y + wv.h) continue;

            double k = sp * 24.0;               // длина по скорости ветра
            if (k > 1.0) k = 1.0;
            const double len = half * 0.9 * k;
            const double nx = ux / sp, ny = uy / sp;

            // Сжатие — тёплым, разрежение — холодным.
            const Color c = (air.pv[i] > 0.0) ? Color{255, 115, 64}
                                              : Color{90, 180, 255};
            const int ax = static_cast<int>(px - nx * len);
            const int ay = static_cast<int>(py - ny * len);
            const int bx = static_cast<int>(px + nx * len);
            const int by = static_cast<int>(py + ny * len);
            line(cv, ax, ay, bx, by, c, 215);
            const double pxp = -ny, pyp = nx;
            const double hl = len * 0.45;
            line(cv, bx, by, static_cast<int>(bx + (-nx + pxp * 0.6) * hl),
                             static_cast<int>(by + (-ny + pyp * 0.6) * hl), c, 215);
            line(cv, bx, by, static_cast<int>(bx + (-nx - pxp * 0.6) * hl),
                             static_cast<int>(by + (-ny - pyp * 0.6) * hl), c, 215);
        }
    }
}

// ---------- экран ХИМИЯ ----------

void Ui::layoutChem(int screenW, int screenH) {
    chemButtons.clear();
    const int pad = 4;
    const int topH = std::clamp(screenH / 18, 28, 70);

    Button close;
    close.w = screenW / 5; close.h = topH - 8;
    close.x = screenW - close.w - 6; close.y = 4;
    close.act = Act::ChemClose;
    close.label = "ЗАКРЫТЬ";
    close.color = Color{70, 48, 48};
    chemButtons.push_back(close);

    // Сетка: 18 столбцов, 10 рядов — с лантаноидами и актиноидами снизу.
    const int y0 = topH + pad * 2;
    const int cs = std::min((screenW - pad * 2) / 18, (screenH - y0 - pad * 8) / 10);
    const int x0 = (screenW - cs * 18) / 2;
    for (int i = 0; i < CHEM_CELL_COUNT; ++i) {
        const ChemCell& e = CHEM_CELLS[i];
        Button b;
        b.x = x0 + (e.gx - 1) * cs;
        b.y = y0 + (e.gy - 1) * cs;
        b.w = cs - 1; b.h = cs - 1;
        b.act = Act::ChemPick;
        b.id  = e.id;
        b.label = e.sym;
        b.color = Color{e.r, e.g, e.b};
        chemButtons.push_back(b);
    }
}

const Button* Ui::hitChem(int x, int y) const {
    for (const Button& b : chemButtons)
        if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return &b;
    return nullptr;
}

void Ui::drawChem(Canvas& cv) const {
    cv.fill(0, 0, cv.w, cv.h, Color{16, 17, 21});
    const int sc = std::max(2, cv.h / 240);
    drawText(cv, 8, 8, "ТАБЛИЦА МЕНДЕЛЕЕВА", sc, Color{206, 210, 220});

    for (const Button& b : chemButtons) {
        const bool on = (b.act == Act::ChemPick && b.id == selected);
        Color face = b.color;
        if (b.act == Act::ChemPick && !on) {
            // Цвет CPK приглушаем: на светлом подпись иначе не читается.
            face = Color{static_cast<uint8_t>(b.color.r * 0.55),
                         static_cast<uint8_t>(b.color.g * 0.55),
                         static_cast<uint8_t>(b.color.b * 0.55)};
        }
        cv.fill(b.x, b.y, b.w, b.h, face);
        if (on) cv.frame(b.x, b.y, b.w, b.h, Color{255, 255, 255});
        else if (b.act == Act::ChemClose) cv.frame(b.x, b.y, b.w, b.h, Color{150, 90, 90});
        const int ssc = fitScale(b.label, b.w, 3);
        const int tw = textWidth(b.label, ssc), th = 7 * ssc;
        drawText(cv, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2, b.label, ssc,
                 labelColor(face));
    }

    const ChemCell* cur = nullptr;
    for (int i = 0; i < CHEM_CELL_COUNT; ++i)
        if (CHEM_CELLS[i].id == selected) { cur = &CHEM_CELLS[i]; break; }
    char buf[128];
    if (cur) std::snprintf(buf, sizeof buf, "%d  %s  %s", cur->z, cur->sym, cur->name);
    else     std::snprintf(buf, sizeof buf, "%s", "ВЫБЕРИ ЭЛЕМЕНТ — ОН ВСТАНЕТ В ЯЧЕЙКУ ХИМИЯ");
    const int tw = textWidth(buf, sc);
    drawText(cv, (cv.w - tw) / 2, cv.h - 7 * sc - 10, buf, sc,
             cur ? Color{226, 230, 238} : Color{130, 136, 148});
}

// ---------- менеджер сохранений (ветка UptA) ----------

void Ui::layoutManager(int screenW, int screenH, int count) {
    mgrButtons.clear();
    const int pad = 6;
    const int topH = std::clamp(screenH / 18, 28, 70);

    Button close;
    close.w = screenW / 5; close.h = topH - 8;
    close.x = screenW - close.w - pad; close.y = 4;
    close.act = Act::MgrClose;
    close.label = "ЗАКРЫТЬ";
    close.color = Color{70, 48, 48};
    mgrButtons.push_back(close);

    // По строке на сохранение: сама строка грузит, крестик удаляет.
    const int rowH = std::clamp(screenH / 12, 34, 96);
    const int y0 = topH + pad * 2;
    const int delW = rowH;
    for (int i = 0; i < count; ++i) {
        const int y = y0 + i * (rowH + 2);
        if (y + rowH > screenH - pad) break;         // что не влезло — не рисуем
        Button row;
        row.x = pad; row.y = y;
        row.w = screenW - pad * 2 - delW - 4; row.h = rowH;
        row.act = Act::MgrLoad; row.id = i;
        mgrButtons.push_back(row);

        Button del;
        del.x = screenW - pad - delW; del.y = y;
        del.w = delW; del.h = rowH;
        del.act = Act::MgrDelete; del.id = i;
        del.label = "X";
        del.color = Color{88, 40, 40};
        mgrButtons.push_back(del);
    }
}

const Button* Ui::hitManager(int x, int y) const {
    for (const Button& b : mgrButtons)
        if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return &b;
    return nullptr;
}

void Ui::drawManager(Canvas& cv, const std::vector<std::string>& lines) const {
    cv.fill(0, 0, cv.w, cv.h, Color{16, 17, 21});
    const int sc = std::max(2, cv.h / 240);
    drawText(cv, 8, 8, "СОХРАНЕНИЯ", sc, Color{206, 210, 220});

    for (const Button& b : mgrButtons) {
        if (b.act == Act::MgrLoad) {
            cv.fill(b.x, b.y, b.w, b.h, Color{32, 36, 44});
            cv.frame(b.x, b.y, b.w, b.h, Color{58, 64, 76});
            const size_t k = static_cast<size_t>(b.id);
            if (k < lines.size()) {
                const int ssc = std::max(1, std::min(sc, (b.h - 6) / 7));
                drawText(cv, b.x + 8, b.y + (b.h - 7 * ssc) / 2, lines[k], ssc,
                         Color{214, 218, 226});
            }
        } else {
            cv.fill(b.x, b.y, b.w, b.h, b.color);
            cv.frame(b.x, b.y, b.w, b.h, Color{150, 90, 90});
            const int ssc = fitScale(b.label, b.w, 3);
            const int tw = textWidth(b.label, ssc), th = 7 * ssc;
            drawText(cv, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2, b.label, ssc,
                     Color{236, 220, 220});
        }
    }

    if (lines.empty()) {
        const char* s = "СОХРАНЕНИЙ ПОКА НЕТ";
        const int tw = textWidth(s, sc);
        drawText(cv, (cv.w - tw) / 2, cv.h / 2, s, sc, Color{130, 136, 148});
    }
}

}  // namespace upt
