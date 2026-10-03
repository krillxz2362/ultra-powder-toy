// МОДУЛЬ 3: состояние — тепло, фазы, срок жизни
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

}  // namespace upt
