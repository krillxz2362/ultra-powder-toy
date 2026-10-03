// МОДУЛЬ 1: контакт — опора, удар, скольжение
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

}  // namespace upt
