// МОДУЛЬ 2: движение — тяжесть, давление, ветер, перенос
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
        // В моделях ВОДЫ 2.0 жидкость двигает только новый решатель.
        // Если оставить и старый перенос, два решателя начинают
        // спорить: старый затыкает пробоину целой частицей, новый
        // пытается через неё течь, а объём при этом растёт из ниоткуда.
        if (st == LIQUID && liquidModel != LIQ_CELLS) continue;

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
        // В моделях ВОДЫ 2.0 жидкость двигает только новый решатель.
        // Если оставить и старый перенос, два решателя начинают
        // спорить: старый затыкает пробоину целой частицей, новый
        // пытается через неё течь, а объём при этом растёт из ниоткуда.
        if (st == LIQUID && liquidModel != LIQ_CELLS) continue;

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

}  // namespace upt
