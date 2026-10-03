// ВОДА 2.0: три прототипа жидкости, стоящие рядом.
//
// Замысел и правила перехода — в ЖИДКОСТЬ.md. Здесь только счёт.
//
// Главный закон, которому подчинено всё остальное:
//     объём = сумма долей по клеткам + сумма объёмов капель
// Если это число гуляет, вода то появляется, то исчезает, и никакое
// красивое поведение этого не искупит. Поэтому каждый переток написан
// так, чтобы отданное и принятое были одним и тем же числом.
#include <algorithm>
#include <cmath>

#include "upt_core.h"
#include "upt_substances.h"

namespace upt {

namespace {
// Насколько клетка может быть переполнена под давлением. У воды
// сжимаемость крошечная: доля выше единицы означает нагрузку.
constexpr double OVERFILL = 1.08;
// Ниже этой доли клетка считается пустой: остаток уходит соседу,
// иначе по миру расползается невидимая плёнка в тысячные доли.
constexpr double DROPLET  = 0.02;
// Сколько доли утекает за шаг при полном перепаде.
constexpr double FLOW_DOWN = 0.60;
constexpr double FLOW_SIDE = 0.28;
}

double World::liquidVolume() const {
    double v = 0.0;
    if (liquidModel == LIQ_CELLS) {
        for (int i = 0; i < maxUsed; ++i)
            if (alive[i] == 1 && SUBSTANCES[type[i]].state == LIQUID) v += 1.0;
    } else {
        for (int i = 0; i < maxUsed; ++i)
            if (alive[i] == 1 && SUBSTANCES[type[i]].state == LIQUID)
                v += (fill.empty() ? 1.0 : fill[i]);
    }
    for (const Drop& d : drops) v += d.vol;
    return v;
}

// --------------------------------------------------------------------
// Модель FILL: клетка держит долю, перетоки дробные.
//
// Частица остаётся на месте и служит «сосудом»: у неё есть вещество,
// температура и химия. Двигается не она, а её содержимое. Когда доля
// опускается до нуля, частица умирает; когда соседняя клетка получает
// долю, а частицы там нет, она рождается.
// --------------------------------------------------------------------
void World::liquidFill() {
    if (fill.size() < static_cast<size_t>(maxp_)) fill.resize(maxp_, 1.0);
    const int w = w_, h = h_;

    // Проход снизу вверх: вода уходит вниз, и разбирать её надо с
    // нижних рядов — та же причина, что у падающей стопки песка.
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const int i = at(x, y);
            if (i < 0 || SUBSTANCES[type[i]].state != LIQUID) continue;
            double have = fill[i];
            if (have <= 0.0) continue;

            // 1. Вниз. Отдаём столько, сколько примет нижняя клетка.
            if (y + 1 < h) {
                const int below = at(x, y + 1);
                if (below < 0) {
                    // Пусто: переливаем и заводим там частицу.
                    const double move = have * FLOW_DOWN;
                    if (move > DROPLET) {
                        const int k = create(x, y + 1, type[i], shd[i]);
                        if (k >= 0) {
                            if (fill.size() < static_cast<size_t>(maxp_)) fill.resize(maxp_, 1.0);
                            fill[k] = move;
                            tmp[k] = tmp[i];
                            have -= move;
                            wakeCell(x, y + 1);
                        }
                    }
                } else if (type[below] == type[i]) {
                    const double room = OVERFILL - fill[below];
                    if (room > 0.0) {
                        const double move = std::min(have * FLOW_DOWN, room);
                        if (move > 0.0) {
                            fill[below] += move;
                            have -= move;
                            wakeCell(x, y + 1);
                        }
                    }
                }
            }

            // 2. Выброс капли.
            //
            // Клетка способна передать соседу лишь долю своего объёма
            // за шаг. У пробоины напор гонит воду быстрее: по
            // Торричелли скорость равна корню из 2gh, и при глубине в
            // двадцать клеток это две с половиной клетки за шаг.
            // Столько сетка не пропустит — значит, избыток должен
            // улететь каплей. Отсюда и берётся струя.
            if (liquidModel == LIQ_HYBRID && drops.size() < dropLimit
                && have > 0.2) {
                const size_t ci = static_cast<size_t>(y) * w + x;
                const double depth = lp[ci] / std::max(1.0, dens[i]);
                const double v = std::min(std::sqrt(2.0 * GRAV * std::max(0.0, depth)),
                                          static_cast<double>(MAXV));
                if (v > 1.0) {
                    for (int s2 = 0; s2 < 2; ++s2) {
                        const int nx = (s2 == 0) ? x - 1 : x + 1;
                        if (nx < 0 || nx >= w) continue;
                        // Сосед должен быть свободен или почти свободен.
                        // Тонкая плёнка в проёме струю не отменяет —
                        // именно она и есть начало струи.
                        const int oNb = at(nx, y);
                        if (oNb >= 0) {
                            if (SUBSTANCES[type[oNb]].state != LIQUID) continue;
                            if (type[oNb] != type[i]) continue;
                            if (fill.empty() || fill[oNb] > 0.6) continue;
                        }
                        // Улетает то, что сетка передать не успевает.
                        const double excess = std::min(have * 0.5, (v - 1.0) * 0.25);
                        if (excess < 0.05) break;
                        Drop d;
                        d.x = nx + 0.5; d.y = y + 0.5;
                        d.vx = (s2 == 0) ? -v : v;
                        d.vy = 0.0;
                        d.vol = excess;
                        d.type = type[i];
                        drops.push_back(d);
                        have -= excess;
                        break;
                    }
                }
            }

            // 3. Вбок — выравнивание уровня. Отдаём половину разницы:
            // так вода приходит к ровному уровню и не раскачивается.
            for (int s = 0; s < 2 && have > DROPLET; ++s) {
                const int nx = (s == 0) ? x - 1 : x + 1;
                if (nx < 0 || nx >= w) continue;
                const int side = at(nx, y);
                if (side < 0) {
                    const double move = have * FLOW_SIDE;
                    if (move > DROPLET) {
                        const int k = create(nx, y, type[i], shd[i]);
                        if (k >= 0) {
                            if (fill.size() < static_cast<size_t>(maxp_)) fill.resize(maxp_, 1.0);
                            fill[k] = move;
                            tmp[k] = tmp[i];
                            have -= move;
                            wakeCell(nx, y);
                        }
                    }
                } else if (type[side] == type[i]) {
                    const double diff = have - fill[side];
                    if (diff > 0.0) {
                        const double move = std::min(diff * 0.5 * FLOW_SIDE,
                                                     OVERFILL - fill[side]);
                        if (move > 0.0) {
                            fill[side] += move;
                            have -= move;
                            wakeCell(nx, y);
                        }
                    }
                }
            }

            fill[i] = have;
            // Остаток меньше капли отдавать уже некому — клетка пустеет.
            if (fill[i] <= DROPLET) {
                // Отдаём остаток соседу, чтобы объём не пропал.
                bool given = false;
                const int dd[4][2] = {{0,1},{-1,0},{1,0},{0,-1}};
                for (auto& d : dd) {
                    const int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const int o = at(nx, ny);
                    if (o >= 0 && type[o] == type[i]) {
                        fill[o] += fill[i];
                        given = true;
                        break;
                    }
                }
                if (given || fill[i] <= 1e-9) {
                    fill[i] = 0.0;
                    killIndex(i);
                }
            }
        }
    }
}

// --------------------------------------------------------------------
// Модель DROPS: свободные капли вне сетки.
//
// Капля ничего не решает: летит по тяжести, бьётся о те же клетки, что
// песчинка, и возвращается в толщу, когда ей есть куда влиться.
// --------------------------------------------------------------------
void World::dropsStep() {
    if (drops.empty()) return;
    const int w = w_, h = h_;

    auto solid = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return true;
        const int o = at(x, y);
        if (o < 0) return false;
        const int st = SUBSTANCES[type[o]].state;
        return st == SOLID || SUBSTANCES[type[o]].fixed != 0;
    };

    size_t keep = 0;
    for (size_t k = 0; k < drops.size(); ++k) {
        Drop d = drops[k];
        d.vy += GRAV;
        const double sp = std::sqrt(d.vx * d.vx + d.vy * d.vy);
        if (sp > MAXV) { d.vx *= MAXV / sp; d.vy *= MAXV / sp; }

        // Путь проходим шажками: иначе капля прошьёт стену насквозь.
        const int n = static_cast<int>(sp / 0.4) + 1;
        bool landed = false, merged = false;
        for (int s = 1; s <= n && !landed && !merged; ++s) {
            const double nx = d.x + d.vx * s / n;
            const double ny = d.y + d.vy * s / n;
            const int cx = static_cast<int>(nx), cy = static_cast<int>(ny);
            if (cx < 0 || cy < 0 || cx >= w || cy >= h) { landed = true; break; }

            if (solid(cx, cy)) { landed = true; break; }

            const int o = at(cx, cy);
            if (o >= 0 && type[o] == d.type) {
                // Влилась в свою же толщу.
                if (fill.size() < static_cast<size_t>(maxp_)) fill.resize(maxp_, 1.0);
                // Объём отдан здесь — и больше нигде. Ровно эта
                // двойная выдача (на лету и ещё раз при посадке)
                // рождала воду из ниоткуда.
                fill[o] += d.vol;
                wakeCell(cx, cy);
                merged = true;
                break;
            }
            d.x = nx; d.y = ny;
        }

        if (merged) continue;      // объём уже отдан толще
        if (landed) {
            // Садимся туда, где стоим: объём возвращается в толщу.
            const int cx = std::clamp(static_cast<int>(d.x), 0, w - 1);
            const int cy = std::clamp(static_cast<int>(d.y), 0, h - 1);
            const int o = at(cx, cy);
            if (o >= 0 && type[o] == d.type) {
                if (fill.size() < static_cast<size_t>(maxp_)) fill.resize(maxp_, 1.0);
                fill[o] += d.vol;
            } else if (o < 0) {
                const int i2 = create(cx, cy, d.type);
                if (i2 >= 0) {
                    if (fill.size() < static_cast<size_t>(maxp_)) fill.resize(maxp_, 1.0);
                    fill[i2] = d.vol;
                } else {
                    // Места нет — держим каплю, объём не теряем.
                    drops[keep++] = d;
                    continue;
                }
            } else {
                drops[keep++] = d;      // чужое вещество: ждём
                continue;
            }
            wakeCell(cx, cy);
            continue;                   // капля израсходована
        }
        drops[keep++] = d;
    }
    drops.resize(keep);
}

}  // namespace upt
