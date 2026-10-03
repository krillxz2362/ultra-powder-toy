#include "upt_ui.h"

#include <algorithm>
#include <cmath>

#include "upt_core.h"
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
        const Glyph* g = findGlyph(cp);
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

    rowH_ = static_cast<int>(screenH * 0.052);
    rowH_ = std::clamp(rowH_, 30, 46);
    ctrlH_ = rowH_ - 2;
    panelH_ = ctrlH_ + rows * rowH_;
    y0_ = screenH - panelH_;

    buttons.clear();
    const double colW = static_cast<double>(screenW) / cols_;

    for (int i = 0; i < favN; ++i) {
        const int id = favourites[i];
        const Substance& S = SUBSTANCES[id];
        const int c = i % cols_, r = i / cols_;
        Button b;
        b.x = static_cast<int>(c * colW);
        b.y = y0_ + ctrlH_ + r * rowH_;
        b.w = static_cast<int>((c + 1) * colW) - b.x;
        b.h = rowH_;
        b.act = Act::Material;
        b.id = id;
        b.label = S.name;
        b.color = Color{static_cast<uint8_t>(S.r), static_cast<uint8_t>(S.g),
                        static_cast<uint8_t>(S.b)};
        buttons.push_back(b);
    }
    {   // ячейка выхода в таблицу химии
        const int c = favN % cols_, r = favN / cols_;
        Button b;
        b.x = static_cast<int>(c * colW);
        b.y = y0_ + ctrlH_ + r * rowH_;
        b.w = static_cast<int>((c + 1) * colW) - b.x;
        b.h = rowH_;
        b.act = Act::Chem;
        b.label = "ХИМИЯ";
        b.color = Color{60, 70, 90};
        buttons.push_back(b);
    }

    // Ряд управления. Веса те же, что в LÖVE: подписи разной длины, и
    // равные доли резали бы «ОЧИСТИТЬ».
    const char* names[] = {"ПАУЗА", "ШАГ", "<", "КИСТЬ", ">", "ВИД", "ПОТОК", "ОЧИСТИТЬ"};
    const Act acts[] = {Act::Pause, Act::Step, Act::BrushDown, Act::None,
                        Act::BrushUp, Act::View, Act::Arrows, Act::Clear};
    const double wq[] = {1.1, 0.7, 0.5, 0.8, 0.5, 0.85, 0.9, 1.25};
    const int n = 8;
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
        if (b.act == Act::None) text = "КИСТЬ " + std::to_string(brush);
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

}  // namespace upt
