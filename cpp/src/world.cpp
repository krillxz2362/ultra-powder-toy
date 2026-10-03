// МОДУЛЬ 0: мир — частицы, карта клеток, пробуждение, кадр
//
// Часть ядра ULTRA POWDER TOY. Разделение на модули — не косметика:
// у каждого свой договор, что он читает и что пишет. Договоры описаны
// в МОДУЛИ.md, и нарушать их нельзя, иначе код снова станет
// непонятным.
#include "upt_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "upt_chem.h"
#include "upt_substances.h"

namespace upt {


// ---------------------------------------------------------------------
// Воздух
// ---------------------------------------------------------------------
void Air::init(int w, int h) {
    cw  = (w + AIR_CELL - 1) / AIR_CELL;
    chh = (h + AIR_CELL - 1) / AIR_CELL;
    n   = cw * chh;
    pv.assign(n, 0.0);  vx.assign(n, 0.0);  vy.assign(n, 0.0);
    at.assign(n, ROOM); ox.assign(n, 1.0);
    opv.assign(n, 0.0); ovx.assign(n, 0.0); ovy.assign(n, 0.0);
    oat.assign(n, ROOM); oox.assign(n, 1.0);
    wall.assign(n, 0.0);
    adens.assign(n, 12.0);
}

void Air::clear() {
    std::fill(pv.begin(), pv.end(), 0.0);
    std::fill(vx.begin(), vx.end(), 0.0);
    std::fill(vy.begin(), vy.end(), 0.0);
    std::fill(wall.begin(), wall.end(), 0.0);
    std::fill(at.begin(), at.end(), ROOM);
    std::fill(ox.begin(), ox.end(), 1.0);
    std::fill(adens.begin(), adens.end(), 12.0);
}

void Air::blast(int x, int y, double power) {
    const int cx = x / AIR_CELL, cy = y / AIR_CELL;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= cw || ny >= chh) continue;
            const double k = (dx == 0 && dy == 0) ? 1.0 : 0.45;
            const int i = ny * cw + nx;
            double v = pv[i] + power * k;
            if (v > AIR_PMAX) v = AIR_PMAX;
            pv[i] = v;
        }
    }
}

// Порядок шага: подъёмная сила -> перепад давления -> перенос потока
// самим собой -> перенос примесей -> расхождение -> размытие -> потери
// и края. Температура воздуха — то, без чего конвекции не выходит: дым
// иначе поднимается лишь потому, что ему прописана малая плотность.
void Air::update() {
    // 1. подъёмная сила и 2. перепад давления
    for (int y = 1; y <= chh - 2; ++y) {
        const int row = y * cw;
        for (int x = 1; x <= cw - 2; ++x) {
            const int i = row + x;
            vx[i] += TSTEPP * (pv[i - 1] - pv[i + 1]);
            vy[i] += TSTEPP * (pv[i - cw] - pv[i + cw]);
            vy[i] -= BUOY * (at[i] - ROOM);
        }
    }

    // 3-4. перенос потоком: скорость, давление, температура, кислород
    ovx = vx; ovy = vy; oat = at; oox = ox; opv = pv;
    for (int y = 1; y <= chh - 2; ++y) {
        const int row = y * cw;
        for (int x = 1; x <= cw - 2; ++x) {
            const int i = row + x;
            // скорость храним в клетках мира, ячейка вчетверо крупнее
            double tx = x - vx[i] * ADV / AIR_CELL;
            double ty = y - vy[i] * ADV / AIR_CELL;
            if (tx < 0.5) tx = 0.5; else if (tx > cw - 1.5)  tx = cw - 1.5;
            if (ty < 0.5) ty = 0.5; else if (ty > chh - 1.5) ty = chh - 1.5;
            int x0 = static_cast<int>(std::floor(tx));
            int y0 = static_cast<int>(std::floor(ty));
            double fx = tx - x0, fy = ty - y0;
            int a = y0 * cw + x0, b = a + 1, c = a + cw, d = c + 1;
            double w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy);
            double w01 = (1 - fx) * fy,       w11 = fx * fy;
            vx[i] = vx[i] * (1 - ADV) + ADV *
                (ovx[a]*w00 + ovx[b]*w10 + ovx[c]*w01 + ovx[d]*w11);
            vy[i] = vy[i] * (1 - ADV) + ADV *
                (ovy[a]*w00 + ovy[b]*w10 + ovy[c]*w01 + ovy[d]*w11);

            // Давление тоже сносит потоком, иначе у волны нет инерции:
            // взрыв раздувается ровным кругом и гаснет на месте.
            pv[i] = pv[i] * (1 - PADV) + PADV *
                (opv[a]*w00 + opv[b]*w10 + opv[c]*w01 + opv[d]*w11);

            // примеси сносит сильнее: они не сопротивляются потоку
            double sxx = x - vx[i] * SADV / AIR_CELL;
            double syy = y - vy[i] * SADV / AIR_CELL;
            if (sxx < 0.5) sxx = 0.5; else if (sxx > cw - 1.5)  sxx = cw - 1.5;
            if (syy < 0.5) syy = 0.5; else if (syy > chh - 1.5) syy = chh - 1.5;
            x0 = static_cast<int>(std::floor(sxx));
            y0 = static_cast<int>(std::floor(syy));
            fx = sxx - x0; fy = syy - y0;
            a = y0 * cw + x0; b = a + 1; c = a + cw; d = c + 1;
            w00 = (1 - fx) * (1 - fy); w10 = fx * (1 - fy);
            w01 = (1 - fx) * fy;       w11 = fx * fy;
            // Выборку взвешиваем по свободному объёму: вытянуть воздух
            // из каменной стены нельзя. Иначе запаянная банка сосёт
            // кислород прямо из собственных стенок.
            const double f00 = w00 * (1 - wall[a]);
            const double f10 = w10 * (1 - wall[b]);
            const double f01 = w01 * (1 - wall[c]);
            const double f11 = w11 * (1 - wall[d]);
            const double fs = f00 + f10 + f01 + f11;
            if (fs > 0.001) {
                const double inv = 1.0 / fs;
                at[i] = (oat[a]*f00 + oat[b]*f10 + oat[c]*f01 + oat[d]*f11) * inv;
                ox[i] = (oox[a]*f00 + oox[b]*f10 + oox[c]*f01 + oox[d]*f11) * inv;
            }
        }
    }

    // 5. расхождение потока меняет давление
    for (int y = 1; y <= chh - 2; ++y) {
        const int row = y * cw;
        for (int x = 1; x <= cw - 2; ++x) {
            const int i = row + x;
            const double dp = (vx[i - 1] - vx[i + 1]) + (vy[i - cw] - vy[i + cw]);
            double v = pv[i] + TSTEPV * dp;
            if (v > AIR_PMAX) v = AIR_PMAX; else if (v < -AIR_PMAX) v = -AIR_PMAX;
            pv[i] = v;
        }
    }

    // 6. размытие давления, тепла и кислорода. Перенос ослаблен ровно
    // настолько, насколько соседи забиты веществом.
    opv = pv; oat = at; oox = ox;
    for (int y = 1; y <= chh - 2; ++y) {
        const int row = y * cw;
        for (int x = 1; x <= cw - 2; ++x) {
            const int i = row + x;
            const double fr = 1 - wall[i];
            const double kL = (1 - wall[i - 1])  * fr;
            const double kR = (1 - wall[i + 1])  * fr;
            const double kU = (1 - wall[i - cw]) * fr;
            const double kD = (1 - wall[i + cw]) * fr;
            // Углы берём с половинным весом: размытие только по четырём
            // соседям оставляет сетку, и круглая волна идёт ромбом.
            const double kA = (1 - wall[i - cw - 1]) * fr * 0.5;
            const double kB = (1 - wall[i - cw + 1]) * fr * 0.5;
            const double kC = (1 - wall[i + cw - 1]) * fr * 0.5;
            const double kE = (1 - wall[i + cw + 1]) * fr * 0.5;
            const double sw = kL + kR + kU + kD + kA + kB + kC + kE;
            if (sw <= 0.001) continue;
            double f = sw / 6;
            if (f > 1) f = 1;
            const double inv = 1.0 / sw;
            const int il = i - 1,  ir = i + 1;
            const int iu = i - cw, id = i + cw;
            const int ia = iu - 1, ib = iu + 1;
            const int ic = id - 1, ie = id + 1;

            double avg = (kL*opv[il] + kR*opv[ir] + kU*opv[iu] + kD*opv[id]
                        + kA*opv[ia] + kB*opv[ib] + kC*opv[ic] + kE*opv[ie]) * inv;
            pv[i] = opv[i] + 0.40 * f * (avg - opv[i]);

            avg = (kL*oat[il] + kR*oat[ir] + kU*oat[iu] + kD*oat[id]
                 + kA*oat[ia] + kB*oat[ib] + kC*oat[ic] + kE*oat[ie]) * inv;
            at[i] = oat[i] + TDIFF * f * (avg - oat[i]);

            avg = (kL*oox[il] + kR*oox[ir] + kU*oox[iu] + kD*oox[id]
                 + kA*oox[ia] + kB*oox[ib] + kC*oox[ic] + kE*oox[ie]) * inv;
            ox[i] = oox[i] + ODIFF * f * (avg - oox[i]);
        }
    }

    // 7. потери, плотность воздуха и края мира
    const double AIRD0 = 12.0;
    for (int i = 0; i < n; ++i) {
        adens[i] = AIRD0 * 295.0 / (at[i] + 273.0);
        pv[i] *= PLOSS;
        vx[i] *= VLOSS;
        vy[i] *= VLOSS;
        at[i] += (ROOM - at[i]) * TRELAX;
        if (ox[i] < 0) ox[i] = 0; else if (ox[i] > 1) ox[i] = 1;
    }
    // на краях мира — обычный воздух: свежий, комнатный, неподвижный
    for (int x = 0; x < cw; ++x) {
        const int b = (chh - 1) * cw + x;
        vx[x] = vy[x] = pv[x] = 0; at[x] = ROOM; ox[x] = 1;
        vx[b] = vy[b] = pv[b] = 0; at[b] = ROOM; ox[b] = 1;
    }
    for (int y = 0; y < chh; ++y) {
        const int l = y * cw, r = l + cw - 1;
        vx[l] = vy[l] = pv[l] = 0; at[l] = ROOM; ox[l] = 1;
        vx[r] = vy[r] = pv[r] = 0; at[r] = ROOM; ox[r] = 1;
    }
}

// ---------------------------------------------------------------------
// Мир
// ---------------------------------------------------------------------
World::World(int w, int h) : w_(w), h_(h) {
    maxp_ = w * h;
    type.assign(maxp_, 0);
    alive.assign(maxp_, 0);
    px.assign(maxp_, 0.0);
    py.assign(maxp_, 0.0);
    vx.assign(maxp_, 0.0);
    vy.assign(maxp_, 0.0);
    tmp.assign(maxp_, ROOM);
    tmp2.assign(maxp_, ROOM);
    residue.assign(maxp_, 0);
    life.assign(maxp_, 0);
    lat.assign(maxp_, 0.0);
    burn.assign(maxp_, 0);
    burnT.assign(maxp_, 0.0);
    shd.assign(maxp_, 128);
    dens.assign(maxp_, 0.0);
    dir.assign(maxp_, 0);
    settled.assign(maxp_, 0);
    held.assign(maxp_, 0);
    head.assign(maxp_, 0.0);
    tl.assign(maxp_, 4);
    sx.assign(maxp_, 0.0);
    sy.assign(maxp_, 0.0);
    hit.assign(maxp_, 0.0);

    pmap_.assign(static_cast<size_t>(w) * h, 0);
    lp.assign(static_cast<size_t>(w) * h, 0);
    ctop.assign(static_cast<size_t>(w) * h, 0);
    coltop.assign(static_cast<size_t>(w) * h, 0);
    cstack.assign(static_cast<size_t>(w) * h, 0);
    clist.assign(static_cast<size_t>(w) * h, 0);
    rowcnt.assign(static_cast<size_t>(h), 0);

    cw = (w + CHUNK - 1) / CHUNK;
    ch = (h + CHUNK - 1) / CHUNK;
    nchunks = cw * ch;
    awake.assign(nchunks, AWAKE);
    therm.assign(nchunks, 0);
    thermTmp_.assign(nchunks, 0);

    air.init(w, h);
    acw = air.cw;
    ach = air.chh;

    // Свободные номера выдаём с конца, как в Lua-версии: порядок выдачи
    // влияет на порядок обхода частиц, а значит и на результат.
    free_.resize(maxp_);
    for (int i = 0; i < maxp_; ++i) free_[i] = maxp_ - 1 - i;
    freeTop_ = maxp_;
}

void World::wakeChunk(int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) return;
    awake[static_cast<size_t>(cy) * cw + cx] = AWAKE;
}

void World::markAt(std::vector<uint8_t>& map, int x, int y) {
    const int cx = x >> CSHIFT, cy = y >> CSHIFT;
    auto put = [&](int ax, int ay) { map[static_cast<size_t>(ay) * cw + ax] = AWAKE; };
    put(cx, cy);
    // Соседей трогаем, только если клетка стоит на границе куска: иначе
    // на каждую частицу приходилось бы девять записей. Углы обязательны —
    // частица на углу граничит сразу с тремя кусками, и без диагонали
    // один остаётся спать.
    const int mx = x & CMASK, my = y & CMASK;
    const bool left  = (mx == 0)     && cx > 0;
    const bool right = (mx == CMASK) && cx < cw - 1;
    const bool up    = (my == 0)     && cy > 0;
    const bool down  = (my == CMASK) && cy < ch - 1;
    if (left)  put(cx - 1, cy);
    if (right) put(cx + 1, cy);
    if (up) {
        put(cx, cy - 1);
        if (left)  put(cx - 1, cy - 1);
        if (right) put(cx + 1, cy - 1);
    }
    if (down) {
        put(cx, cy + 1);
        if (left)  put(cx - 1, cy + 1);
        if (right) put(cx + 1, cy + 1);
    }
}

void World::wakeCell(int x, int y) { markAt(awake, x, y); }

int World::create(int x, int y, int t, int rnd) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return -1;
    if (t == 0) return -1;
    const size_t ci = static_cast<size_t>(y) * w_ + x;
    if (pmap_[ci] != 0) return -1;
    if (freeTop_ <= 0) return -1;

    const int i = free_[--freeTop_];
    type[i]    = static_cast<uint16_t>(t);
    alive[i]   = 1;
    px[i]      = x + 0.5;
    py[i]      = y + 0.5;
    vx[i]      = 0.0;
    vy[i]      = 0.0;
    tmp[i]     = SUBSTANCES[t].temp0;
    lat[i]     = 0.0;
    life[i]    = SUBSTANCES[t].life;
    shd[i]     = static_cast<uint8_t>(rnd >= 0 ? rnd : 128);
    dir[i]     = static_cast<uint8_t>((rnd >= 0 ? rnd : 0) % 2);
    residue[i] = 0;
    burn[i]    = 0;
    settled[i] = 0;

    pmap_[ci]  = i + 1;
    if (i >= maxUsed) maxUsed = i + 1;
    ++count;

    wakeChunkArea(x / CHUNK, y / CHUNK);
    // Горячее или холодное вещество обязано разбудить тепловую метку:
    // без этого остывание на спящем куске просто не считается.
    const double tp = tmp[i];
    if (tp > ROOM + 1 || tp < ROOM - 1)
        therm[(y / CHUNK) * cw + (x / CHUNK)] = AWAKE;
    return i;
}

// Будит куст 3x3 вокруг куска. Отдельно от wakeCell: при рождении
// частицы соседние куски будятся всегда, а не только с границы.
void World::wakeChunkArea(int cx, int cy) {
    int x0 = cx - 1, x1 = cx + 1, y0 = cy - 1, y1 = cy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cw - 1) x1 = cw - 1;
    if (y1 > ch - 1) y1 = ch - 1;
    for (int y = y0; y <= y1; ++y) {
        const int r = y * cw;
        for (int x = x0; x <= x1; ++x) awake[r + x] = AWAKE;
    }
}

void World::killIndex(int i) {
    if (i < 0 || alive[i] == 0) return;
    int x = static_cast<int>(std::floor(px[i]));
    int y = static_cast<int>(std::floor(py[i]));
    if (x >= 0 && y >= 0 && x < w_ && y < h_) {
        size_t ci = static_cast<size_t>(y) * w_ + x;
        if (pmap_[ci] == i + 1) pmap_[ci] = 0;
        wakeCell(x, y);
    }
    alive[i] = 0;
    free_[freeTop_++] = i;
    --count;
}

void World::killAt(int x, int y) {
    int i = at(x, y);
    if (i >= 0) killIndex(i);
}

void World::convert(int i, int t) {
    if (t == 0) { killIndex(i); return; }
    type[i] = static_cast<uint16_t>(t);
    life[i] = SUBSTANCES[t].life;
    lat[i]  = 0.0;
    wakeCell(static_cast<int>(std::floor(px[i])),
             static_cast<int>(std::floor(py[i])));
}

// ---------------------------------------------------------------------
// Отрисовка: заполнение кадра
// ---------------------------------------------------------------------
static std::vector<uint8_t> g_palette;

const uint8_t* palette() {
    if (!g_palette.empty()) return g_palette.data();
    g_palette.assign(static_cast<size_t>(SUBSTANCE_COUNT) * SHADES * 3, 0);
    for (int id = 0; id < SUBSTANCE_COUNT; ++id) {
        double cr = BG_R, cg = BG_G, cb = BG_B, amp = 0;
        if (id != 0) {
            cr = SUBSTANCES[id].r;
            cg = SUBSTANCES[id].g;
            cb = SUBSTANCES[id].b;
            amp = SUBSTANCES[id].shade;
        }
        for (int sh = 0; sh < SHADES; ++sh) {
            // Разброс оттенка вокруг основного цвета: без него песок
            // выглядит залитым одной краской, а не сыпучим.
            const double k = (static_cast<double>(sh) / (SHADES - 1) - 0.5) * 2 * amp;
            const size_t o = (static_cast<size_t>(id) * SHADES + sh) * 3;
            auto clamp255 = [](double v) -> uint8_t {
                if (v < 0) return 0;
                if (v > 255) return 255;
                return static_cast<uint8_t>(v);
            };
            g_palette[o]     = clamp255(cr + k);
            g_palette[o + 1] = clamp255(cg + k);
            g_palette[o + 2] = clamp255(cb + k);
        }
    }
    return g_palette.data();
}

// Цветовые шкалы режимов. Точки шкалы те же, что в game/ui/render.lua.
namespace {

struct Stop { double at; uint8_t r, g, b; };

constexpr Stop HEAT_STOPS[] = {
    {-273,  10,  10,  60}, {   0,  30,  60, 180}, {  22,  40, 110, 200},
    { 100,  40, 190, 190}, { 300,  80, 220,  80}, { 700, 240, 220,  60},
    {1200, 250, 140,  30}, {2000, 255,  80,  40}, {3000, 255, 255, 255},
};
constexpr Stop PRES_STOPS[] = {
    {-60,  20,  40, 200}, {-12,  30,  90, 170}, {  0,  16,  16,  20},
    { 12, 170,  90,  30}, { 60, 240, 200,  80}, {200, 255, 255, 255},
};
constexpr Stop OXY_STOPS[] = {
    {0.00,  90,  20,  20}, {0.12, 150,  60,  20}, {0.30, 170, 150,  40},
    {0.70,  60, 120, 110}, {1.00,  30,  60, 120},
};

template <size_t N>
void ramp(const Stop (&st)[N], double v, uint8_t* out) {
    const Stop* a = &st[0];
    const Stop* b = &st[N - 1];
    for (size_t i = 0; i + 1 < N; ++i)
        if (v >= st[i].at && v <= st[i + 1].at) { a = &st[i]; b = &st[i + 1]; break; }
    if (v <= st[0].at)     { a = b = &st[0]; }
    if (v >= st[N - 1].at) { a = b = &st[N - 1]; }
    const double span = b->at - a->at;
    const double f = (span > 0.0) ? (v - a->at) / span : 0.0;
    out[0] = static_cast<uint8_t>(a->r + (b->r - a->r) * f);
    out[1] = static_cast<uint8_t>(a->g + (b->g - a->g) * f);
    out[2] = static_cast<uint8_t>(a->b + (b->b - a->b) * f);
}

}  // namespace

void World::fillFrame(uint8_t* rgba, int view) const {
    const uint8_t* pal = palette();
    const size_t n = static_cast<size_t>(w_) * h_;

    if (view == VIEW_HEAT) {
        uint8_t room[3];
        ramp(HEAT_STOPS, ROOM, room);
        for (size_t i = 0; i < n; ++i) {
            rgba[i*4] = room[0]; rgba[i*4+1] = room[1];
            rgba[i*4+2] = room[2]; rgba[i*4+3] = 255;
        }
        for (int i = 0; i < maxUsed; ++i) {
            if (alive[i] != 1) continue;
            const int x = static_cast<int>(px[i]), y = static_cast<int>(py[i]);
            if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
            uint8_t c[3];
            ramp(HEAT_STOPS, tmp[i], c);
            const size_t o = (static_cast<size_t>(y) * w_ + x) * 4;
            rgba[o] = c[0]; rgba[o+1] = c[1]; rgba[o+2] = c[2];
        }
        return;
    }

    if (view == VIEW_PRES || view == VIEW_OXY) {
        // Давление и кислород есть и там, где вещества нет, — здесь
        // приходится красить каждую клетку, а не идти по частицам.
        for (int y = 0; y < h_; ++y) {
            const int arow = (y >> ASHIFT) * air.cw;
            for (int x = 0; x < w_; ++x) {
                const int a = arow + (x >> ASHIFT);
                uint8_t c[3];
                if (view == VIEW_PRES) ramp(PRES_STOPS, air.pv[a], c);
                else                   ramp(OXY_STOPS, air.ox[a], c);
                const size_t o = (static_cast<size_t>(y) * w_ + x) * 4;
                rgba[o] = c[0]; rgba[o+1] = c[1]; rgba[o+2] = c[2]; rgba[o+3] = 255;
            }
        }
        // вещество поверх поля — полутоном, чтобы было видно и то и другое
        for (int i = 0; i < maxUsed; ++i) {
            if (alive[i] != 1) continue;
            const int x = static_cast<int>(px[i]), y = static_cast<int>(py[i]);
            if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
            const size_t k = (static_cast<size_t>(type[i]) * SHADES + (shd[i] >> 3)) * 3;
            const size_t o = (static_cast<size_t>(y) * w_ + x) * 4;
            rgba[o]   = static_cast<uint8_t>((rgba[o]   + pal[k])     / 2);
            rgba[o+1] = static_cast<uint8_t>((rgba[o+1] + pal[k + 1]) / 2);
            rgba[o+2] = static_cast<uint8_t>((rgba[o+2] + pal[k + 2]) / 2);
        }
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        rgba[i*4] = BG_R; rgba[i*4+1] = BG_G; rgba[i*4+2] = BG_B; rgba[i*4+3] = 255;
    }
    // Идём по частицам, а не по клеткам: пустоты обычно больше, чем
    // вещества, и красить её по второму разу незачем.
    for (int i = 0; i < maxUsed; ++i) {
        if (alive[i] != 1) continue;
        const size_t k = (static_cast<size_t>(type[i]) * SHADES + (shd[i] >> 3)) * 3;
        const int x = static_cast<int>(std::floor(px[i]));
        const int y = static_cast<int>(std::floor(py[i]));
        if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
        const size_t o = (static_cast<size_t>(y) * w_ + x) * 4;
        rgba[o]     = pal[k];
        rgba[o + 1] = pal[k + 1];
        rgba[o + 2] = pal[k + 2];
    }
}


void World::step() {
    support();
    liquidPressure();
    heat();
    react();
    mix();
    densities();
    couple();
    clampWall();
    air.update();
    move();

    // ВОДА 2.0. Три прототипа стоят рядом и включаются полем
    // liquidModel: так их можно сравнить на одной сцене, а не спорить.
    // Разбор — в ЖИДКОСТЬ.md.
    if (liquidModel == LIQ_FILL || liquidModel == LIQ_HYBRID) liquidFill();
    if (liquidModel == LIQ_DROPS || liquidModel == LIQ_HYBRID) dropsStep();
}

}  // namespace upt
