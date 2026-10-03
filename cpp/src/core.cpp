#include "upt_core.h"
#include "upt_chem.h"

#include <algorithm>
#include <cmath>

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
// Теплообмен
// ---------------------------------------------------------------------
void World::heat() {
    const int mu = maxUsed;
    if (mu == 0) return;

    std::copy(tmp.begin(), tmp.begin() + mu, tmp2.begin());

    for (int i = 0; i < mu; ++i) {
        if (!alive[i]) continue;
        const int t = type[i];
        const int x = static_cast<int>(std::floor(px[i]));
        const int y = static_cast<int>(std::floor(py[i]));
        if (therm[static_cast<size_t>(y >> CSHIFT) * cw + (x >> CSHIFT)] == 0) continue;

        const double ti = tmp[i];
        const double ci = SUBSTANCES[t].cond;
        double acc = 0.0;
        const size_t base = static_cast<size_t>(y) * w_ + x;
        int open = 0;   // сколько сторон открыто воздуху

        auto side = [&](int32_t occ) {
            if (occ > 0) {
                const int o = occ - 1;
                double k = SUBSTANCES[type[o]].cond;
                if (ci < k) k = ci;
                acc += (tmp[o] - ti) * k;
            } else if (occ == 0) {
                ++open;
            }
        };

        side(x > 0      ? pmap_[base - 1]  : -1);
        side(x < w_ - 1 ? pmap_[base + 1]  : -1);
        side(y > 0      ? pmap_[base - w_] : -1);
        side(y < h_ - 1 ? pmap_[base + w_] : -1);

        // Обмен с воздухом. Энергия сохраняется: сколько частица взяла,
        // столько воздух отдал.
        if (open > 0) {
            const size_t ai = static_cast<size_t>(y >> ASHIFT) * acw + (x >> ASHIFT);
            const double ta = air.at[ai];
            const double k  = (ci < AIR_COND ? ci : AIR_COND) * open * 0.25;
            const double q  = (ta - ti) * k;
            acc += q;
            air.at[ai] = ta - q * FLOW * SUBSTANCES[t].cap / CAIR;
        }

        // Теплоёмкость: то же тепло поднимает температуру воды вчетверо
        // слабее, чем железа. Делитель не опускаем ниже единицы, иначе
        // явная схема теряет устойчивость.
        if (SUBSTANCES[t].fixed == 0) {
            double cp = SUBSTANCES[t].cap;
            if (cp < 1.0) cp = 1.0;
            double nt = ti + acc * (FLOW / cp);
            nt = nt + (ROOM - nt) * BODY_RELAX;
            if (nt < TMIN) nt = TMIN; else if (nt > TMAX) nt = TMAX;
            tmp2[i] = nt;
        }
    }

    tmp.swap(tmp2);

    // Метки: где есть тепло — там считаем теплообмен в следующий раз,
    // где есть перепад по вертикали у жидкости или газа — там будим
    // движение, иначе уснувший бак перестаёт перемешиваться.
    std::fill(therm.begin(), therm.end(), 0);
    for (int i = 0; i < mu; ++i) {
        if (!alive[i]) continue;
        const double tv = tmp[i];
        if (tv <= ROOM + 1 && tv >= ROOM - 1) continue;

        const int x = static_cast<int>(std::floor(px[i]));
        const int y = static_cast<int>(std::floor(py[i]));

        markAt(therm, x, y);


        const int st = SUBSTANCES[type[i]].state;
        if ((st == LIQUID || st == GAS) && y < h_ - 1) {
            const int32_t o = pmap_[static_cast<size_t>(y + 1) * w_ + x];
            if (o != 0) {
                const double d = tv - tmp[o - 1];
                if (d > 2.0 || d < -2.0) wakeCell(x, y);
            }
        }
    }
}


// ---------------------------------------------------------------------
// Плотность с поправкой на температуру
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
// Гидростатика. Два шага: вес столба над каждой клеткой и общий уровень
// связной области. Второе — закон сообщающихся сосудов; через поле
// давления он не решается: статическое решение размазывает перепад и не
// выражает того, что уровни ещё не сравнялись.
// ---------------------------------------------------------------------
void World::liquidPressure() {
    const int w = w_, h = h_;

    // Шаг 1. Вес столба жидкости над каждой клеткой. Отсюда берутся
    // боковая сила (струя из пробоины) и местный уровень поверхности.
    for (int x = 0; x < w; ++x) {
        double acc = 0.0;
        int top = -1, gap = 0, i = x;
        for (int y = 0; y < h; ++y) {
            const int o = pmap_[i];
            if (o != 0 && SUBSTANCES[type[o - 1]].state == LIQUID) {
                // Пузырь в один-два ряда столб не разрывает: над ним
                // всё та же вода, и её вес никуда не делся.
                if (top < 0 || gap > GAP_MAX) { top = y; acc = 0.0; }
                gap = 0;
                const double d = SUBSTANCES[type[o - 1]].density;
                lp[i] = static_cast<int32_t>(acc + d * 0.5);
                acc += d;
                ctop[i] = -1;              // пометка «ещё не разобрано»
                coltop[i] = top;
            } else {
                ++gap;
                if (gap > GAP_MAX) { acc = 0.0; top = -1; }
                lp[i] = 0;
                ctop[i] = 32000;
                coltop[i] = y;
            }
            i += w;
        }
    }

    // Шаг 2. Связные области жидкости: вода в одной области сообщается,
    // значит её уровень обязан быть общим.
    for (int y0 = 0; y0 < h; ++y0) {
        const int row0 = y0 * w;
        for (int x0 = 0; x0 < w; ++x0) {
            const int start = row0 + x0;
            if (ctop[start] != -1) continue;

            int sp = 0, nc = 0;
            cstack[sp++] = start;
            ctop[start] = -2;              // взято в обработку
            clist[nc++] = start;
            int minY = y0;
            while (sp > 0) {
                const int c = cstack[--sp];
                const int cy = c / w, cx = c % w;
                if (cy < minY) minY = cy;
                if (cx > 0     && ctop[c - 1] == -1) { ctop[c - 1] = -2; cstack[sp++] = c - 1; clist[nc++] = c - 1; }
                if (cx < w - 1 && ctop[c + 1] == -1) { ctop[c + 1] = -2; cstack[sp++] = c + 1; clist[nc++] = c + 1; }
                if (cy > 0     && ctop[c - w] == -1) { ctop[c - w] = -2; cstack[sp++] = c - w; clist[nc++] = c - w; }
                if (cy < h - 1 && ctop[c + w] == -1) { ctop[c + w] = -2; cstack[sp++] = c + w; clist[nc++] = c + w; }
            }

            // Уровень области. Брать самую верхнюю клетку нельзя: одна
            // подскочившая брызга поднимает «уровень» на десяток клеток,
            // вся вода в баке начинает считать, что отстаёт, и лезть
            // вверх. Новые брызги — новый повод, вода кипит без конца.
            // Поэтому берём не крайний столб, а нижнюю четверть
            // поверхности: отдельные капли в неё не попадают.
            int ntop = 0;
            for (int k = 0; k < nc; ++k) {
                const int c = clist[k], cy = c / w;
                if (coltop[c] == cy) { ++rowcnt[cy]; ++ntop; }
            }
            double need = ntop * 0.25;
            if (need < 1.0) need = 1.0;
            double acc2 = 0.0;
            int level = minY;
            for (int yy = minY; yy < h; ++yy) {
                const int v = rowcnt[yy];
                if (v != 0) { acc2 += v; if (acc2 >= need) { level = yy; break; } }
            }
            for (int yy = minY; yy < h; ++yy) rowcnt[yy] = 0;
            for (int k = 0; k < nc; ++k) ctop[clist[k]] = level;
        }
    }
}

static inline double effDens(int t, double tp) {
    const Substance& S = SUBSTANCES[t];
    if (S.state == GAS) {
        // Уравнение состояния: горячий газ легче холодного.
        double k = T0ABS / (tp + 273.0);
        if (k > 6.0) k = 6.0; else if (k < 0.05) k = 0.05;
        return S.density * k;
    }
    double k = 1.0 - S.expans * THERMAL_GAIN * (tp - 22.0);
    if (k > 1.8) k = 1.8; else if (k < 0.2) k = 0.2;
    return S.density * k;
}

void World::densities() {
    for (int i = 0; i < maxUsed; ++i)
        if (alive[i]) dens[i] = effDens(type[i], tmp[i]);
}

// ---------------------------------------------------------------------
// Силы
// ---------------------------------------------------------------------
void World::forces() {
    const int w = w_, h = h_;
    for (int i = 0; i < maxUsed; ++i) {
        if (!alive[i]) continue;
        const int t = type[i];
        const Substance& S = SUBSTANCES[t];
        const int st = S.state;
        if (st == SOLID) continue;

        const double x = px[i], y = py[i];
        const int ox = static_cast<int>(std::floor(x));
        const int oy = static_cast<int>(std::floor(y));
        if (awake[static_cast<size_t>(oy >> CSHIFT) * cw + (ox >> CSHIFT)] == 0) continue;

        const size_t curI = static_cast<size_t>(oy) * w + ox;

        double dSelf = dens[i];
        if (dSelf < 0.1) dSelf = 0.1;

        // Закон Архимеда: сравниваем себя со средой сверху и снизу.
        double md = 0.0; int mc = 0;
        const size_t aIdx = static_cast<size_t>(oy >> ASHIFT) * acw + (ox >> ASHIFT);
        // Берём заранее посчитанную плотность, а не считаем заново:
        // adens заполняется в air.update() ДО сброса краёв мира, и на
        // краю пересчёт по текущей температуре даёт другое число.
        const double dAir = air.adens[aIdx];
        auto sample = [&](size_t idx) {
            const int32_t o = pmap_[idx];
            if (o == 0) { md += dAir; ++mc; return; }
            const int os = SUBSTANCES[type[o - 1]].state;
            if (os == LIQUID || os == GAS) { md += dens[o - 1]; ++mc; }
        };
        if (oy > 0)     sample(curI - w);
        if (oy < h - 1) sample(curI + w);
        const double medium = mc > 0 ? md / mc : dAir;

        double gEff = 1.0 - medium / dSelf;
        if (gEff > 1.0) gEff = 1.0; else if (gEff < -1.5) gEff = -1.5;
        // Жидкость в самой себе была бы невесомой и перестала бы оседать.
        if (st == LIQUID && gEff >= 0.0 && gEff < 0.15) gEff = 0.15;

        double vxx = vx[i] * S.drag;
        double vyy = (vy[i] + GRAV * gEff * S.grav) * S.drag;

        double headExcess = 0.0;
        int tenLike = 4;

        if (st == LIQUID) {
            // Вязкость как трение, а не как вероятность.
            if (S.visc > 0.0) vxx *= (1.0 - S.visc);

            // Поверхностное натяжение: капля тянется к своим.
            if (S.tension > 0.0) {
                int lf = 0, rf = 0, uf = 0, df = 0;
                if (ox > 0)     { int32_t o = pmap_[curI - 1]; if (o && type[o-1] == t) lf = 1; }
                if (ox < w - 1) { int32_t o = pmap_[curI + 1]; if (o && type[o-1] == t) rf = 1; }
                if (oy > 0)     { int32_t o = pmap_[curI - w]; if (o && type[o-1] == t) uf = 1; }
                if (oy < h - 1) { int32_t o = pmap_[curI + w]; if (o && type[o-1] == t) df = 1; }
                tenLike = lf + rf + uf + df;
                vxx += S.tension * 0.30 * (lf - rf);
                if (tenLike <= 2) vxx *= (1.0 - S.tension);
            }

            // Боковой перепад давления столба.
            const double selfP = lp[curI];
            double pL = selfP, pR = selfP;
            if (ox > 0) {
                const int32_t o = pmap_[curI - 1];
                if (o == 0) pL = 0.0;
                else {
                    const int os2 = SUBSTANCES[type[o-1]].state;
                    if (os2 == LIQUID) pL = lp[curI - 1];
                    else if (os2 == GAS) pL = 0.0;
                }
            }
            if (ox < w - 1) {
                const int32_t o = pmap_[curI + 1];
                if (o == 0) pR = 0.0;
                else {
                    const int os2 = SUBSTANCES[type[o-1]].state;
                    if (os2 == LIQUID) pR = lp[curI + 1];
                    else if (os2 == GAS) pR = 0.0;
                }
            }
            double ap = PRESS_K * GRAV * (pL - pR) / (2.0 * dSelf);
            if (ap > PRESS_MAX) ap = PRESS_MAX; else if (ap < -PRESS_MAX) ap = -PRESS_MAX;
            vxx += ap;

            // Насколько здешний уровень ниже общего уровня области.
            headExcess = static_cast<double>(coltop[curI] - ctop[curI]);
            if (headExcess > 0.5) {
                // Отстающее колено сообщающихся сосудов. Вес столба в
                // покое уравновешен давлением снизу — поднимает воду
                // не «добавка к тяжести», а перекос уровней. Поэтому
                // тяжесть для этой частицы на шаг снимается (её держит
                // столб под ней), а вместо неё ставится ускорение
                // U-образной трубки: a = g·Δh/L, где L — длина пути
                // воды, то есть перекос плюс глубина самого колена.
                // При Δh = L это ровно свободное падение, больше не
                // бывает: формула сама себя ограничивает.
                const double depth = static_cast<double>(h - coltop[curI]);
                double L = headExcess + depth;
                if (L < 1.0) L = 1.0;
                const double au = GRAV * headExcess / L;
                vyy -= GRAV * gEff * S.grav * S.drag;   // снять тяжесть
                vyy -= au * S.drag;
            }
        }

        // Снос ветром: ускорение обратно плотности самой частицы,
        // поэтому дым несёт, а расплав почти нет.
        if (S.advec > 0.0) {
            double ratio = dAir / dSelf;
            if (ratio > 1.0) ratio = 1.0;
            const double kk = S.advec * ratio;
            vxx += kk * (air.vx[aIdx] - vxx);
            vyy += kk * (air.vy[aIdx] - vyy);
        }

        if (S.jit > 0.0) {
            vxx += (rng.next() - 0.5) * S.jit;
            vyy += (rng.next() - 0.5) * S.jit * 0.5;
        }

        const double vmax = (st == LIQUID) ? 3.0 : MAXV;
        if (vxx > vmax) vxx = vmax; else if (vxx < -vmax) vxx = -vmax;
        if (vyy > vmax) vyy = vmax; else if (vyy < -vmax) vyy = -vmax;

        vx[i] = vxx;
        vy[i] = vyy;
        head[i] = headExcess;
        tl[i] = static_cast<uint8_t>(tenLike);
    }
}

// ---------------------------------------------------------------------
// Перенос
// ---------------------------------------------------------------------
void World::advect() {
    const int w = w_, h = h_;

    // Обход снизу вверх. Стопка падающих частиц держит сама себя:
    // верхняя не сдвинется, пока не освободит клетку нижняя. Если
    // разбирать стопку снизу, вся цепочка съезжает за один шаг и
    // пласт не расползается по высоте.
    order_.clear();
    rowCnt_.assign(static_cast<size_t>(h) + 1, 0);
    for (int i = 0; i < maxUsed; ++i) {
        if (!alive[i]) continue;
        if (SUBSTANCES[type[i]].state == SOLID) continue;
        int yy = static_cast<int>(py[i]);
        if (yy < 0) yy = 0; else if (yy >= h) yy = h - 1;
        ++rowCnt_[static_cast<size_t>(yy)];
    }
    size_t acc = 0;
    for (int y = h - 1; y >= 0; --y) {
        const size_t c = rowCnt_[static_cast<size_t>(y)];
        rowCnt_[static_cast<size_t>(y)] = acc;
        acc += c;
    }
    order_.assign(acc, 0);
    for (int i = 0; i < maxUsed; ++i) {
        if (!alive[i]) continue;
        if (SUBSTANCES[type[i]].state == SOLID) continue;
        int yy = static_cast<int>(py[i]);
        if (yy < 0) yy = 0; else if (yy >= h) yy = h - 1;
        order_[rowCnt_[static_cast<size_t>(yy)]++] = i;
    }

    for (size_t oIdx = 0; oIdx < order_.size(); ++oIdx) {
        const int i = order_[oIdx];
        if (!alive[i]) continue;
        const int t = type[i];
        const Substance& S = SUBSTANCES[t];
        const int st = S.state;
        if (st == SOLID) continue;

        int ox = static_cast<int>(std::floor(px[i]));
        int oy = static_cast<int>(std::floor(py[i]));
        if (awake[static_cast<size_t>(oy >> CSHIFT) * cw + (ox >> CSHIFT)] == 0) continue;

        const int dt = SUBSTANCES[t].density;
        const double headExcess = head[i];
        const int tenLike = tl[i];
        double dSelf = dens[i];
        if (dSelf < 0.1) dSelf = 0.1;

        const double el = S.elast;
        double sp = std::fabs(vx[i]);
        const double q = std::fabs(vy[i]);
        if (q > sp) sp = q;
        int steps = static_cast<int>(std::ceil(sp));
        if (steps < 1) steps = 1;
        if (steps > 7) steps = 7;

        sx[i] = vx[i] / steps;
        sy[i] = vy[i] / steps;
        hit[i] = 0.0;
        const int ox0 = ox, oy0 = oy;

        for (int s = 0; s < steps; ++s) {
            const double xx = px[i], yy = py[i];
            double ssx = sx[i], ssy = sy[i];
            int cx = static_cast<int>(std::floor(xx));
            int cy = static_cast<int>(std::floor(yy));
            size_t cI = static_cast<size_t>(cy) * w + cx;
            double nx = xx + ssx, ny = yy + ssy;
            int tx = static_cast<int>(std::floor(nx));
            int ty = static_cast<int>(std::floor(ny));

            if (tx == cx && ty == cy) { px[i] = nx; py[i] = ny; continue; }

            if (tx < 0 || tx >= w) {
                vx[i] = -vx[i] * el;
                ssx = -ssx; sx[i] = ssx;
                nx = (tx < 0) ? 0.01 : w - 0.01;
                tx = static_cast<int>(std::floor(nx));
            }
            if (ty < 0 || ty >= h) {
                vy[i] = -vy[i] * el;
                ssy = -ssy; sy[i] = ssy;
                ny = (ty < 0) ? 0.01 : h - 0.01;
                ty = static_cast<int>(std::floor(ny));
            }

            const size_t nI = static_cast<size_t>(ty) * w + tx;
            const int32_t occ = pmap_[nI];

            if (occ == 0) {
                pmap_[cI] = 0;
                pmap_[nI] = i + 1;
                px[i] = nx; py[i] = ny;
                settled[i] = 0;
                continue;
            }

            const int oi = occ - 1;
            const int ot = type[oi];
            const int ost = SUBSTANCES[ot].state;

            bool swap = false;
            if (ost == LIQUID || ost == GAS) {
                const double odE = dens[oi];
                const double lim = dSelf * SWAP_MIN;
                if (ssy > 0.0 && odE < dSelf - lim) swap = true;
                else if (ssy < 0.0 && odE > dSelf + lim) swap = true;
                else if (ost == GAS && st != GAS) swap = true;
            }

            if (swap) {
                px[oi] = cx + 0.5; py[oi] = cy + 0.5;
                pmap_[cI] = oi + 1;
                pmap_[nI] = i + 1;
                vx[oi] = vx[oi] * 0.5 + vx[i] * 0.2;
                vy[oi] = vy[oi] * 0.5 + vy[i] * 0.2;
                vx[i] *= 0.7; vy[i] *= 0.7;
                sx[i] = ssx * 0.7; sy[i] = ssy * 0.7;
                px[i] = nx; py[i] = ny;
                continue;
            }

            hit[i] = std::fabs(vx[i]) + std::fabs(vy[i]);

            // Закон сохранения импульса. Если то, во что мы упёрлись,
            // само ни на что не опирается, то и опоры нет: удар
            // неупругий, обе частицы идут дальше с общей скоростью
            // (m1 v1 + m2 v2) / (m1 + m2). Если сосед опёрт, за ним
            // стоит земля, его масса всё равно что бесконечна — и мы
            // встаём. Это та же формула на пределе, отдельного правила
            // не нужно.
            // Удар бывает только при сближении. Если сосед уходит от
            // нас не медленнее, чем мы идём к нему, мы его не догоняем
            // и никакого касания нет — клетка занята лишь потому, что
            // мир нарезан на клетки. Гасить скорость тут не за что.
            const double rel = (tx - cx) * (vx[i] - vx[oi])
                             + (ty - cy) * (vy[i] - vy[oi]);
            const bool mobile = (SUBSTANCES[ot].fixed == 0)
                             && (SUBSTANCES[ot].state != SOLID);
            const bool closing = rel > 0.0;
            // Свободная пара: оба ни на что не опираются и сближаются.
            const bool freePair = mobile && held[oi] == 0 && held[i] == 0;
            if (freePair && closing) {
                const double m1 = dens[i], m2 = dens[oi];
                const double msum = m1 + m2;
                const double ux = (m1 * vx[i] + m2 * vx[oi]) / msum;
                const double uy = (m1 * vy[i] + m2 * vy[oi]) / msum;
                vx[i] = ux;  vy[i] = uy;
                vx[oi] = ux; vy[oi] = uy;
            }

            // Удар расталкивает улежавшееся.
            if (ost == POWDER && settled[oi] == 1 && hit[i] > 0.4
                && rng.next() < SUBSTANCES[ot].repose) {
                settled[oi] = 0;
                wakeCell(tx, ty);
            }

            bool slid = false;

            // Вода под напором поднимается: меняется местами с тем, что над ней.
            if (st == LIQUID && headExcess > 2.0 && cy > 0
                && coltop[cI] == cy) {
                const int32_t up = pmap_[cI - w];
                if (up != 0) {
                    const int ui = up - 1;
                    if (SUBSTANCES[type[ui]].state == LIQUID) {
                        px[ui] = cx + 0.5;
                        py[ui] = cy + 0.5;
                        pmap_[cI] = ui + 1;
                        pmap_[cI - w] = i + 1;
                        cI -= w;
                        cy -= 1;
                        px[i] = cx + 0.5;
                        py[i] = cy + 0.5;
                        vy[i] = -0.2;
                        slid = true;
                    }
                }
            }

            // Угол естественного откоса: сыпучее соскальзывает не всегда.
            // Скольжение — это скатывание по куче, и оно требует опоры.
            // Если под нами такая же свободно падающая частица, кучи
            // нет и скатываться не с чего: в свободном падении крупинки
            // не расталкивают друг друга вбок.
            // Запрет на скольжение — только для свободного падения:
            // две летящие рядом крупинки не расталкивают друг друга.
            // Лежащей жидкости скольжение, наоборот, необходимо: она
            // растекается именно им, иначе лужа застывает горбом.
            const bool freeFall = freePair && !closing;
            bool maySlide = (st == POWDER || st == LIQUID) && !freeFall;
            if (st == POWDER) {
                if (S.repose == 0.0) maySlide = false;   // льдина: падает, но не растекается
            } else if (maySlide) {
                if (rng.next() < S.visc) {
                    maySlide = false;
                } else if (S.tension > 0.0 && tenLike <= 2 && rng.next() < S.tension) {
                    maySlide = false;
                }
            }

            if (maySlide) {
                const int pref = (dir[i] == 0) ? -1 : 1;
                // Проверка slid стоит в КОНЦЕ витка, а не в условии цикла:
                // частица, поднявшаяся под напором, обязана успеть ещё и
                // скользнуть вбок — именно так она переливается в соседнее
                // колено. Условие в заголовке цикла отнимало у неё этот шаг.
                for (int k = 1; k <= 2; ++k) {
                    const int sdx = (k == 1) ? pref : -pref;
                    const int cxx = cx + sdx;
                    // Через край не ходим — но выйти по continue нельзя:
                    // внизу витка стоит проверка slid, и частица, уже
                    // поднявшаяся под напором, иначе уедет во вторую
                    // сторону вместо того, чтобы остаться на месте.
                    if (cxx >= 0 && cxx < w) {
                    const int tries = (st == LIQUID) ? 2 : 1;
                    for (int tt = 1; tt <= tries; ++tt) {
                        const int cyy = (tt == 1) ? (cy + 1) : cy;
                        if (cyy < 0 || cyy >= h) continue;
                        const size_t dI = static_cast<size_t>(cyy) * w + cxx;
                        const int32_t co = pmap_[dI];
                        bool ok = false;
                        if (co == 0) ok = true;
                        else {
                            const int c3 = type[co - 1];
                            const int cs = SUBSTANCES[c3].state;
                            if ((cs == LIQUID || cs == GAS) && SUBSTANCES[c3].density < dt) ok = true;
                        }
                        // Крутой обрыв осыпается всегда, пологий склон — по подвижности.
                        if (ok && st == POWDER) {
                            const bool deep = (cyy + 1 < h)
                                && pmap_[static_cast<size_t>(cyy + 1) * w + cxx] == 0;
                            if (!deep) {
                                if (settled[i] == 1) ok = false;
                                else if (rng.next() >= S.repose) { settled[i] = 1; ok = false; }
                            }
                        }
                        if (ok) {
                            if (co != 0) {
                                const int oi2 = co - 1;
                                px[oi2] = cx + 0.5;
                                py[oi2] = cy + 0.5;
                                pmap_[cI] = oi2 + 1;
                            } else {
                                pmap_[cI] = 0;
                            }
                            pmap_[dI] = i + 1;
                            px[i] = cxx + 0.5;
                            py[i] = cyy + 0.5;
                            if (cyy > cy && freePair) {
                                // Скатывание по диагонали: это поворот
                                // скорости, а не её потеря. Частица
                                // сохраняет величину скорости, теряя на
                                // трении, и уходит под 45 градусов.
                                const double fr = 1.0 - 0.25 * S.visc;
                                vy[i] *= fr;
                                vx[i] = vx[i] * fr + sdx * 0.05;
                            } else {
                                // Растекание вбок по ровному: прежняя подача.
                                vx[i] = sdx * (std::fabs(vy[i]) * 0.4 + 0.25) * (1.0 - S.visc);
                                vy[i] *= 0.3;
                            }
                            slid = true;
                            break;
                        }
                    }
                    }
                    if (slid) break;
                }
                if (!slid) dir[i] = 1 - dir[i];
            } else if (st == GAS) {
                const int sdx = (rng.next() < 0.5) ? -1 : 1;
                const int cxx = cx + sdx;
                if (cxx >= 0 && cxx < w && pmap_[static_cast<size_t>(cy) * w + cxx] == 0) {
                    pmap_[cI] = 0;
                    pmap_[static_cast<size_t>(cy) * w + cxx] = i + 1;
                    px[i] = cxx + 0.5;
                    vx[i] = sdx * 0.4;
                    slid = true;
                }
            }

            if (!slid && !freeFall && !(freePair && closing)) {
                // Отскок бывает только от опоры: от свободно падающего
                // соседа отскакивать не от чего.
                vx[i] *= 0.4;
                vy[i] = -vy[i] * el;
            }
            break;
        }

        ox = static_cast<int>(std::floor(px[i]));
        oy = static_cast<int>(std::floor(py[i]));
        const bool movedCell = (ox != ox0) || (oy != oy0);

        // Взрыв от удара переедет вместе с фазой react: пока только
        // пробуждение, чтобы ничего не делать молча и не наврать.
        if (movedCell) wakeCell(ox, oy);
    }

    for (int c = 0; c < nchunks; ++c)
        if (awake[c] != 0) awake[c] -= 1;
}

// ---------------------------------------------------------------------
// Опора: кто на кого опирается
// ---------------------------------------------------------------------
void World::support() {
    const int w = w_, h = h_;
    // Снизу вверх по каждому столбцу: опора передаётся только вверх,
    // поэтому одного прохода хватает.
    for (int x = 0; x < w; ++x) {
        bool below = true;              // под нижним рядом — край мира
        for (int y = h - 1; y >= 0; --y) {
            const int32_t o = pmap_[static_cast<size_t>(y) * w + x];
            if (o == 0) { below = false; continue; }
            const int i = o - 1;
            const Substance& S = SUBSTANCES[type[i]];
            // Неподвижное вещество само себе опора и держит всё сверху.
            const bool fixedBody = (S.fixed != 0) || (S.state == SOLID);
            held[i] = (fixedBody || below) ? 1 : 0;
            below = held[i] != 0;
            // Неопёртая частица по определению падает, а значит не
            // имеет права спать: в уснувшем куске силы не считаются,
            // и тяжесть к ней просто не применяется. Пласт тогда не
            // падает целым, а осыпается рядами по мере пробуждения.
            if (held[i] == 0 && !fixedBody
                && (S.state == POWDER || S.state == LIQUID))
                wakeCell(x, y);
        }
    }
}

// ---------------------------------------------------------------------
// 5. РЕАКЦИИ: горение, фазовые переходы, кислота, растворение
// ---------------------------------------------------------------------
void World::react() {
    const int w = w_, h = h_;
    const int mu = maxUsed;

    for (int i = 0; i < mu; ++i) {
        if (alive[i] != 1) continue;
        int t = type[i];
        double tp = tmp[i];
        const int lf = SUBSTANCES[t].life;

        if (!(lf > 0 || SUBSTANCES[t].active == 1 || t == ID_ACID
              || tp > ROOM + 1 || tp < ROOM - 1)) continue;

        const int x = static_cast<int>(std::floor(px[i]));
        const int y = static_cast<int>(std::floor(py[i]));
        const int base = y * w + x;

        if (t == ID_FIRE) {
            const int ai = (y >> ASHIFT) * acw + (x >> ASHIFT);
            // Горение сжигает кислород: в запаянной банке огонь гаснет
            // сам, а у приоткрытой щели держится.
            double o2 = air.ox[ai] - SUBSTANCES[ID_FIRE].oxyUse;
            if (o2 < 0) o2 = 0;
            air.ox[ai] = o2;
            bool wet = false;
            if (x > 0)     { int o = pmap_[base - 1]; if (o && type[o-1] == ID_WATER) wet = true; }
            if (!wet && x < w - 1) { int o = pmap_[base + 1]; if (o && type[o-1] == ID_WATER) wet = true; }
            if (!wet && y > 0)     { int o = pmap_[base - w]; if (o && type[o-1] == ID_WATER) wet = true; }
            if (!wet && y < h - 1) { int o = pmap_[base + w]; if (o && type[o-1] == ID_WATER) wet = true; }
            if (wet && rng.next() < 0.70) {
                // залило: пламя гаснет, остаётся пар
                if (rng.next() < 0.5) { convert(i, ID_STEAM); tmp[i] = 110; }
                else                  { killIndex(i); }
            } else if (o2 < SUBSTANCES[ID_FIRE].oxyNeed) {
                // Задохнулся. Без кислорода древесина не сгорает, а
                // обугливается: так и делают древесный уголь.
                const int res = residue[i];
                if (res > 0)                  convert(i, res);
                else if (rng.next() < 0.35)   convert(i, ID_SMOKE);
                else                          killIndex(i);
            } else if (tp < 600) {
                tp += (900 - tp) * 0.5;
                tmp[i] = tp;
            }
        }

        // идёт реакция горения: вещество держит свою температуру
        if (burn[i] > 0) {
            const double bt = burnT[i];
            if (tp < bt) { tp = bt; tmp[i] = bt; }
            burn[i] -= 1;
        }

        // срок жизни
        if (lf > 0) {
            if (life[i] > 0) {
                life[i] -= 1;
            } else if (t == ID_FIRE) {
                const int res = residue[i];
                if (res > 0 && rng.next() < 0.30) convert(i, res);
                else if (rng.next() < 0.35)       convert(i, ID_SMOKE);
                else                              killIndex(i);
            } else {
                const int to = SUBSTANCES[t].decayTo;
                if (to == 0) killIndex(i); else convert(i, to);
            }
        }

        if (alive[i] != 1) continue;
        t = type[i];
        const Substance& S = SUBSTANCES[t];
        const double cp = S.cap;
        const int st = S.state;

        // Давление поднимает точку кипения, разрежение опускает:
        // в вакууме вода закипает холодной, как в жизни.
        double bo = S.boilAt;
        if (bo < 99999) bo += air.pv[air.index(x, y)] * PRESS_BOIL;

        if (tp >= bo) {
            // Скрытая теплота парообразования: пока она не набрана,
            // температура стоит на точке кипения и не растёт.
            lat[i] += (tp - bo) * cp;
            tmp[i] = bo;
            if (lat[i] >= S.latV * LAT_SCALE) {
                lat[i] = 0;
                // Из выкипевшего рассола соль никуда не девается.
                if (t == ID_BRINE && rng.next() < 0.25) {
                    convert(i, ID_SALT); tmp[i] = bo;
                } else {
                    convert(i, S.boilTo); tmp[i] = bo + 2;
                }
            }
        } else if (tp >= S.meltAt) {
            lat[i] += (tp - S.meltAt) * cp;
            tmp[i] = S.meltAt;
            if (lat[i] >= S.latF * LAT_SCALE) {
                lat[i] = 0;
                convert(i, S.meltTo);
                tmp[i] = S.meltAt + 2;
            }
        } else if (tp <= S.coolAt) {
            // обратный переход: отдаём ту же скрытую теплоту
            const double need = ((st == GAS) ? S.latV : S.latF) * LAT_SCALE;
            lat[i] += (S.coolAt - tp) * cp;
            tmp[i] = S.coolAt;
            if (lat[i] >= need) {
                lat[i] = 0;
                convert(i, S.coolTo);
                tmp[i] = S.coolAt - 2;
            }
        } else {
            if (lat[i] != 0.0) lat[i] *= 0.97;

            // Соль растворяется в воде: рассол тяжелее воды, замерзает
            // при минус восьми и при выкипании оставляет соль обратно.
            if (t == ID_SALT) {
                int j = -1;
                const int r = rng.next(4);
                if      (r == 1 && x > 0)     j = base - 1;
                else if (r == 2 && x < w - 1) j = base + 1;
                else if (r == 3 && y > 0)     j = base - w;
                else if (r == 4 && y < h - 1) j = base + w;
                if (j >= 0) {
                    const int o = pmap_[j];
                    if (o != 0 && type[o-1] == ID_WATER && rng.next() < 0.12) {
                        convert(o - 1, ID_BRINE);
                        killIndex(i);
                    }
                }
            } else if (t == ID_ICE) {
                // Соль понижает точку замерзания: посыпанный солью лёд
                // тает и в мороз. Так чистят дороги.
                bool melted = false;
                auto saltish = [&](int j) {
                    const int o = pmap_[j];
                    if (o == 0) return false;
                    const int n2 = type[o-1];
                    return n2 == ID_SALT || n2 == ID_BRINE;
                };
                if (x > 0)                 melted = saltish(base - 1);
                if (!melted && x < w - 1)  melted = saltish(base + 1);
                if (!melted && y > 0)      melted = saltish(base - w);
                if (!melted && y < h - 1)  melted = saltish(base + w);
                if (melted && tp > -8 && rng.next() < 0.03) convert(i, ID_WATER);
            } else if ((t == ID_WATER || t == ID_BRINE) && tp > 35) {
                // Испарение с открытой поверхности: лужа сохнет задолго
                // до кипения, и тем быстрее, чем теплее.
                if (y > 0 && pmap_[base - w] == 0
                    && rng.next() < (tp - 35) * 0.00012) {
                    if (t == ID_BRINE && rng.next() < 0.25) {
                        convert(i, ID_SALT);
                    } else {
                        convert(i, ID_STEAM); tmp[i] = tp + 20;
                    }
                }
            }

            if (S.flam > 0) {
                bool hot = tp >= S.igniteAt;
                bool touch = false;
                if (!hot) {
                    // Поджигает не только пламя, но и любой раскалённый
                    // сосед. Через одну теплопроводность это не работает:
                    // сосед успевает остыть.
                    const double ign = S.igniteAt;
                    auto hotAt = [&](int j) {
                        const int o = pmap_[j];
                        if (o == 0) return false;
                        return type[o-1] == ID_FIRE || tmp[o-1] >= ign;
                    };
                    if (x > 0)                touch = hotAt(base - 1);
                    if (!touch && x < w - 1)  touch = hotAt(base + 1);
                    if (!touch && y > 0)      touch = hotAt(base - w);
                    if (!touch && y < h - 1)  touch = hotAt(base + w);
                }
                const int ai2 = (y >> ASHIFT) * acw + (x >> ASHIFT);
                if ((hot || touch) && air.ox[ai2] >= S.oxyNeed
                    && rng.next() < S.flam * (hot ? 1 : 5)) {
                    const int res = S.residue;
                    const double bl = S.blast;
                    const double bt = S.burnTemp;
                    residue[i] = static_cast<uint16_t>(res >= 0 ? res : 0);
                    // Чаще горючее превращается в пламя. Но термит даёт
                    // расплав: газ улетел бы вверх и ничего не прожёг.
                    int bTo = S.burnTo;
                    if (bTo < 0) bTo = ID_FIRE;
                    convert(i, bTo);
                    tmp[i] = bt;
                    const int bLife = S.burnLife;
                    if (bLife > 0) {
                        life[i]  = bLife > 255 ? 255 : bLife;
                        burn[i]  = bLife > 255 ? 255 : bLife;
                        burnT[i] = bt;
                    }
                    wakeCell(x, y);
                    if (bl > 0) explode(x, y, bl);
                }
            } else if (t == ID_ACID) {
                int j = -1;
                const int r = rng.next(4);
                if      (r == 1 && x > 0)     j = base - 1;
                else if (r == 2 && x < w - 1) j = base + 1;
                else if (r == 3 && y > 0)     j = base - w;
                else if (r == 4 && y < h - 1) j = base + w;
                if (j >= 0) {
                    const int o = pmap_[j];
                    if (o != 0) {
                        const int oi = o - 1;
                        const int nb = type[oi];
                        if (nb != ID_ACID && SUBSTANCES[nb].acidProof == 0
                            && rng.next() < 0.25) {
                            killIndex(oi);
                            if (rng.next() < 0.12) killIndex(i);
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------
// 6. ХИМИЯ ПАР И РАЗЛОЖЕНИЕ
// ---------------------------------------------------------------------
void World::mix() {
    const int w = w_, h = h_;
    const int mu = maxUsed;
    if (!chemReady()) buildChemTables();

    for (int i = 0; i < mu; ++i) {
        if (alive[i] != 1 || RX_ACTIVE[type[i]] != 1) continue;
        const int t = type[i];
        const double tp = tmp[i];
        const int x = static_cast<int>(std::floor(px[i]));
        const int y = static_cast<int>(std::floor(py[i]));
        const int base = y * w + x;

        const int di = decayAt(t);
        if (di != 0 && tp >= DECAYS[di-1].tmin && rng.next() < DECAYS[di-1].prob) {
            // Разложение: сама частица становится первым продуктом,
            // второй садится в свободную соседнюю клетку. Места нет —
            // второй продукт теряется, зато сетка цела.
            const Decay& d = DECAYS[di-1];
            convert(i, d.o1);
            if (d.o2 >= 0) {
                int nx = x, ny = y;
                const int r = rng.next(4);
                if      (r == 1) nx = x - 1;
                else if (r == 2) nx = x + 1;
                else if (r == 3) ny = y - 1;
                else             ny = y + 1;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h
                    && pmap_[ny * w + nx] == 0) {
                    // create возвращает -1, когда частица не влезла:
                    // писать температуру по этому номеру нельзя
                    const int j = create(nx, ny, d.o2);
                    if (j >= 0) tmp[j] = tp;
                }
            }
            wakeCell(x, y);
            continue;
        }

        int j = -1;
        const int r = rng.next(4);
        if      (r == 1 && x > 0)     j = base - 1;
        else if (r == 2 && x < w - 1) j = base + 1;
        else if (r == 3 && y > 0)     j = base - w;
        else if (r == 4 && y < h - 1) j = base + w;
        if (j < 0) continue;

        const int o = pmap_[j];
        if (o == 0) continue;
        const int oi = o - 1;
        const int pi = reactionAt(t, type[oi]);
        if (pi == 0) continue;
        const Reaction& rc = REACTIONS[pi-1];
        if (!(tp >= rc.tmin && tp <= rc.tmax && rng.next() < rc.prob)) continue;

        bool ok = true;
        if (rc.catFirst >= 0) {
            // Катализатор сам не расходуется, но без него реакция не
            // идёт: аммиака без железа не получить.
            ok = false;
            auto isCat = [&](int cell) {
                const int q = pmap_[cell];
                if (q == 0) return false;
                const int qt = type[q-1];
                for (int c = 0; c < rc.catCount; ++c)
                    if (CAT_IDS[rc.catFirst + c] == qt) return true;
                return false;
            };
            if (x > 0)             ok = isCat(base - 1);
            if (!ok && x < w - 1)  ok = isCat(base + 1);
            if (!ok && y > 0)      ok = isCat(base - w);
            if (!ok && y < h - 1)  ok = isCat(base + w);
        }
        if (!ok) continue;

        const double heatQ = rc.heat;
        if (rc.o1 >= 0) {
            convert(i, rc.o1);
            double nt = tp + heatQ;
            if (nt > TMAX) nt = TMAX;
            if (nt < TMIN) nt = TMIN;
            tmp[i] = nt;
        } else {
            killIndex(i);
        }
        if (rc.o2 >= 0) {
            convert(oi, rc.o2);
            double nt = tmp[oi] + heatQ;
            if (nt > TMAX) nt = TMAX;
            if (nt < TMIN) nt = TMIN;
            tmp[oi] = nt;
        } else {
            killIndex(oi);
        }
        wakeCell(x, y);
    }
}

// ---------------------------------------------------------------------
// 7. СВЯЗЬ С ВОЗДУХОМ
// ---------------------------------------------------------------------
void World::couple() {
    const int mu = maxUsed;
    std::fill(air.wall.begin(), air.wall.end(), 0.0);

    for (int i = 0; i < mu; ++i) {
        if (alive[i] != 1) continue;
        const Substance& S = SUBSTANCES[type[i]];
        const int ax = static_cast<int>(std::floor(px[i])) >> ASHIFT;
        const int ay = static_cast<int>(std::floor(py[i])) >> ASHIFT;
        if (ax < 0 || ay < 0 || ax >= air.cw || ay >= air.chh) continue;
        const int ai = ay * air.cw + ax;

        // Частица тормозит воздух в своей ячейке и тащит его за собой.
        // У твёрдого потеря равна нулю: оно глушит ветер, как стена.
        air.vx[ai] = air.vx[ai] * S.airLoss + S.airDrag * vx[i];
        air.vy[ai] = air.vy[ai] * S.airLoss + S.airDrag * vy[i];
        if (S.hotAir != 0) air.pv[ai] += S.hotAir;
        if (S.expand != 0) air.pv[ai] += (tmp[i] - ROOM) * S.expand;

        // Заполненность ячейки. Ячейка вчетверо крупнее клетки, поэтому
        // стенка в одну клетку занимает лишь её четверть. Считать её
        // четвертью стены нельзя: сквозь неё пошёл бы кислород. Четырёх
        // клеток твёрдого — сплошной перегородки — уже достаточно.
        if      (S.state == SOLID)  air.wall[ai] += 0.25;
        else if (S.state == LIQUID) air.wall[ai] += 0.10;
        else if (S.state == POWDER) air.wall[ai] += 0.06;
    }
}

void World::clampWall() {
    for (int i = 0; i < air.n; ++i)
        if (air.wall[i] > 0.97) air.wall[i] = 0.97;
}

// ---------------------------------------------------------------------
// 8. ВЗРЫВ
// ---------------------------------------------------------------------
void World::explode(int x, int y, double power) {
    air.blast(x, y, power * BLAST_K);
    int r = static_cast<int>(std::ceil(power * 1.6));
    if (r > 6) r = 6;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
            const int occ = pmap_[static_cast<size_t>(ny) * w_ + nx];
            if (occ != 0) {
                const int oi = occ - 1;
                if (SUBSTANCES[type[oi]].fixed == 0) {
                    tmp[oi] += 600 * (1 - static_cast<double>(d2) / (r * r + 1));
                    double sp = 2.0 * power / (1 + d2);
                    if (sp > 4.0) sp = 4.0;
                    vx[oi] += dx * sp;
                    vy[oi] += dy * sp;
                }
            }
            wakeCell(nx, ny);
        }
    }
}

// Один шаг мира. Порядок фаз тот же, что в эталоне, и менять его
// нельзя: гидростатика считается до того, как что-то сдвинулось,
// а воздух — после того, как вещество его придавило.
void World::clearWorld() {
    for (int i = 0; i < maxUsed; ++i) if (alive[i]) killIndex(i);
    air.clear();
    std::fill(awake.begin(), awake.end(), static_cast<uint8_t>(0));
    std::fill(therm.begin(), therm.end(), static_cast<uint8_t>(0));
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

// ---------------------------------------------------------------------
// Химия: плоская таблица пар
// ---------------------------------------------------------------------
static std::vector<int32_t> g_pairIdx;
static std::vector<int32_t> g_decIdx;

void buildChemTables() {
    const int n = SUBSTANCE_COUNT;
    g_pairIdx.assign(static_cast<size_t>(n) * n, 0);
    for (const PairRef& p : PAIRS) {
        g_pairIdx[static_cast<size_t>(p.a) * n + p.b] = p.rx;
    }
    g_decIdx.assign(n, 0);
    for (int i = 0; i < DECAY_COUNT; ++i) {
        g_decIdx[DECAYS[i].from] = i + 1;
    }
}

bool chemReady() { return !g_pairIdx.empty(); }

int32_t decayAt(int t) {
    if (g_decIdx.empty()) buildChemTables();
    return g_decIdx[t];
}

int32_t reactionAt(int a, int b) {
    if (g_pairIdx.empty()) buildChemTables();
    return g_pairIdx[static_cast<size_t>(a) * SUBSTANCE_COUNT + b];
}

} // namespace upt
