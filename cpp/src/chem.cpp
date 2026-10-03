// МОДУЛИ 4-7: химия — поиск пар, проверка, правила, исполнение
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

}  // namespace upt
