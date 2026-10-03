// МОДУЛЬ 8: тела. Договор — в ТЕЛА.md.
#include "upt_body.h"

#include <algorithm>
#include <cmath>

#include "upt_core.h"
#include "upt_substances.h"

namespace upt {

namespace {
constexpr double DAMP   = 0.995;   // вязкость среды для точек тела
constexpr double BOUNCE = 0.30;    // сколько скорости остаётся при ударе
}

void Bodies::clear() {
    pts.clear(); links.clear(); limits.clear(); nextBody = 1;
}

int Bodies::addPoint(double x, double y, double r, double mass,
                     bool pinned, int body) {
    BodyPoint p;
    p.x = p.ox = x;
    p.y = p.oy = y;
    p.r = r; p.mass = mass;
    p.pinned = pinned ? 1 : 0;
    p.body = body;
    pts.push_back(p);
    return static_cast<int>(pts.size()) - 1;
}

void Bodies::addLink(int a, int b, double stiff) {
    Link l;
    l.a = a; l.b = b; l.stiff = stiff;
    l.rest = std::hypot(pts[a].x - pts[b].x, pts[a].y - pts[b].y);
    links.push_back(l);
}

void Bodies::addLimit(int a, int c, double minDist) {
    AngleLimit g;
    g.a = a; g.c = c; g.minDist = minDist;
    limits.push_back(g);
}

int Bodies::makeRope(double x, double y, int n, double seg) {
    const int body = nextBody++;
    int prev = -1;
    for (int i = 0; i < n; ++i) {
        const int p = addPoint(x, y + i * seg, 0.5, 1.0, i == 0, body);
        if (prev >= 0) addLink(prev, p, 1.0);
        prev = p;
    }
    return body;
}

int Bodies::makeDoll(double x, double y, double s) {
    const int body = nextBody++;
    // Голова, грудь, таз, кисти, стопы. Решётка расстояний, а не
    // скелет с костями: оттого кукла мнётся и складывается.
    const int head  = addPoint(x,            y - 3.0 * s, 0.9 * s, 1.2, false, body);
    const int chest = addPoint(x,            y - 1.5 * s, 0.8 * s, 1.5, false, body);
    const int hip   = addPoint(x,            y,           0.8 * s, 1.5, false, body);
    const int handL = addPoint(x - 1.8 * s,  y - 1.0 * s, 0.6 * s, 0.7, false, body);
    const int handR = addPoint(x + 1.8 * s,  y - 1.0 * s, 0.6 * s, 0.7, false, body);
    const int footL = addPoint(x - 0.9 * s,  y + 2.4 * s, 0.6 * s, 0.9, false, body);
    const int footR = addPoint(x + 0.9 * s,  y + 2.4 * s, 0.6 * s, 0.9, false, body);
    const int elbL  = addPoint(x - 1.0 * s,  y - 1.3 * s, 0.5 * s, 0.6, false, body);
    const int elbR  = addPoint(x + 1.0 * s,  y - 1.3 * s, 0.5 * s, 0.6, false, body);

    addLink(head, chest, 1.0);
    addLink(chest, hip, 1.0);
    addLink(chest, elbL, 0.9);  addLink(elbL, handL, 0.9);
    addLink(chest, elbR, 0.9);  addLink(elbR, handR, 0.9);
    addLink(hip, footL, 0.9);   addLink(hip, footR, 0.9);
    // Поперечины: без них туловище складывается пополам.
    addLink(head, hip, 0.5);
    addLink(elbL, hip, 0.3);    addLink(elbR, hip, 0.3);
    addLink(footL, footR, 0.2);

    // Локоть и колено не выворачиваются назад.
    addLimit(chest, handL, 1.6 * s);
    addLimit(chest, handR, 1.6 * s);
    return body;
}

int Bodies::grab(double x, double y, double maxDist) {
    int best = -1;
    double bd = maxDist * maxDist;
    for (size_t i = 0; i < pts.size(); ++i) {
        const double dx = pts[i].x - x, dy = pts[i].y - y;
        const double d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = static_cast<int>(i); }
    }
    return best;
}

void Bodies::dragTo(int p, double x, double y) {
    if (p < 0 || p >= static_cast<int>(pts.size())) return;
    // Тянем пружиной, а не переносом: тогда брошенное тело летит, а
    // не падает на месте. Прошлое положение не трогаем — разница
    // между ним и новым и есть скорость броска.
    BodyPoint& q = pts[p];
    q.x += (x - q.x) * 0.5;
    q.y += (y - q.y) * 0.5;
}

void Bodies::release(int) {}

int Bodies::submerged(const World& w, int body) const {
    int n = 0;
    for (const BodyPoint& p : pts) {
        if (p.body != body) continue;
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        if (x < 0 || y < 0 || x >= w.width() || y >= w.height()) continue;
        const int o = w.at(x, y);
        if (o >= 0 && SUBSTANCES[w.type[o]].state == LIQUID) ++n;
    }
    return n;
}

void Bodies::step(World& w) {
    const int W = w.width(), H = w.height();

    // 1. Тяжесть и движение по Верле.
    for (BodyPoint& p : pts) {
        if (p.pinned) { p.ox = p.x; p.oy = p.y; continue; }

        double vx = (p.x - p.ox) * DAMP;
        double vy = (p.y - p.oy) * DAMP;

        // Выталкивание: считается по той же плотности среды, что и у
        // частиц. В воде кукла всплывает, в ртути — лежит сверху.
        const int cx = static_cast<int>(p.x), cy = static_cast<int>(p.y);
        double medium = 0.0;
        if (cx >= 0 && cy >= 0 && cx < W && cy < H) {
            const int o = w.at(cx, cy);
            if (o >= 0) {
                const int st = SUBSTANCES[w.type[o]].state;
                // Сыпучее держит лёгкое тело не хуже жидкости: песок
                // плотнее человека, и человек лежит на куче, а не
                // проваливается сквозь неё. Без этого кукла
                // прокапывала кучу до самого дна.
                if (st == LIQUID || st == GAS || st == POWDER) medium = w.dens[o];
            }
        }
        const double bodyDens = (p.dens > 1.0) ? p.dens : 900.0;
        double g = GRAV * (1.0 - medium / bodyDens);
        if (medium > 0.0) { vx *= 0.92; vy *= 0.92; }   // вязкость среды

        p.ox = p.x; p.oy = p.y;
        // Скорость ограничена, как у частиц: без потолка точка за
        // пару сотен шагов разгоняется до десятков клеток за шаг и
        // проходит сквозь пол, не заметив его.
        vy += g;
        const double sp = std::sqrt(vx * vx + vy * vy);
        if (sp > MAXV) { vx *= MAXV / sp; vy *= MAXV / sp; }
        p.x += vx;
        p.y += vy;
    }

    // 2. Связи. Несколько проходов: чем больше, тем жёстче тело.
    for (int it = 0; it < iterations; ++it) {
        for (const Link& l : links) {
            BodyPoint& a = pts[l.a];
            BodyPoint& b = pts[l.b];
            double dx = b.x - a.x, dy = b.y - a.y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d < 1e-9) continue;
            const double diff = (d - l.rest) / d * 0.5 * l.stiff;
            const double wa = a.pinned ? 0.0 : 1.0;
            const double wb = b.pinned ? 0.0 : 1.0;
            const double sum = wa + wb;
            if (sum < 1e-9) continue;
            a.x += dx * diff * (2.0 * wa / sum);
            a.y += dy * diff * (2.0 * wa / sum);
            b.x -= dx * diff * (2.0 * wb / sum);
            b.y -= dy * diff * (2.0 * wb / sum);
        }
        for (const AngleLimit& g : limits) {
            BodyPoint& a = pts[g.a];
            BodyPoint& c = pts[g.c];
            double dx = c.x - a.x, dy = c.y - a.y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d >= g.minDist || d < 1e-9) continue;
            const double push = (g.minDist - d) / d * 0.5;
            a.x -= dx * push; a.y -= dy * push;
            c.x += dx * push; c.y += dy * push;
        }
    }

    // 3. Столкновение с миром и края.
    for (BodyPoint& p : pts) {
        if (p.pinned) continue;

        if (p.x < p.r) { p.x = p.r; p.ox = p.x + (p.x - p.ox) * BOUNCE; }
        if (p.x > W - p.r) { p.x = W - p.r; p.ox = p.x + (p.x - p.ox) * BOUNCE; }
        if (p.y < p.r) { p.y = p.r; p.oy = p.y + (p.y - p.oy) * BOUNCE; }
        if (p.y > H - p.r) { p.y = H - p.r; p.oy = p.y + (p.y - p.oy) * BOUNCE; }

        auto blocked = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= W || y >= H) return true;
            const int o = w.at(x, y);
            if (o < 0) return false;
            const int st = SUBSTANCES[w.type[o]].state;
            return st == SOLID || SUBSTANCES[w.type[o]].fixed != 0;
        };

        // Проход по пути. Точка летит быстрее клетки за шаг, и если
        // смотреть только туда, где она оказалась, пол толщиной в
        // десять клеток она пройдёт насквозь — просто не заметив его.
        // Поэтому идём от прошлого места к новому мелкими шажками и
        // останавливаемся о первое же неподвижное вещество.
        {
            const double dx = p.x - p.ox, dy = p.y - p.oy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            const int n = static_cast<int>(dist / 0.4) + 1;
            double sx = p.ox, sy = p.oy;
            const double stx = dx / n, sty = dy / n;
            for (int k = 1; k <= n; ++k) {
                const double nx2 = p.ox + stx * k, ny2 = p.oy + sty * k;
                if (blocked(static_cast<int>(nx2), static_cast<int>(ny2))) {
                    p.x = sx; p.y = sy;
                    // Гасим скорость: удар о неподвижное.
                    p.ox = p.x + (p.x - p.ox) * BOUNCE;
                    p.oy = p.y + (p.y - p.oy) * BOUNCE;
                    break;
                }
                sx = nx2; sy = ny2;
            }
        }

        const int cx = static_cast<int>(p.x), cy = static_cast<int>(p.y);
        if (cx < 1 || cy < 1 || cx >= W - 1 || cy >= H - 1) continue;

        if (blocked(cx, cy)) {
            const double fx = p.x - cx - 0.5, fy = p.y - cy - 0.5;
            int bestdx = 0, bestdy = 0;
            double best = 1e9;
            const int dd[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& d : dd) {
                if (blocked(cx + d[0], cy + d[1])) continue;
                const double cost = (d[0] ? (d[0] > 0 ? 0.5 - fx : 0.5 + fx)
                                          : (d[1] > 0 ? 0.5 - fy : 0.5 + fy));
                if (cost < best) { best = cost; bestdx = d[0]; bestdy = d[1]; }
            }
            if (best < 1e8) {
                p.x += bestdx * (best + 0.01);
                p.y += bestdy * (best + 0.01);
                // Гасим скорость в сторону стены.
                if (bestdx) p.ox = p.x + (p.x - p.ox) * BOUNCE;
                if (bestdy) p.oy = p.y + (p.y - p.oy) * BOUNCE;
            }
        }

        // Сыпучее и жидкое точка расталкивает: вещество уходит в
        // ближнюю свободную клетку. Так кукла тонет в песке, а не
        // стоит на нём, и оставляет за собой след.
        const int o = w.at(cx, cy);
        if (o >= 0) {
            const int st = SUBSTANCES[w.type[o]].state;
            // Жидкость не расталкиваем: она обтекает сама. Раньше
            // кукла выкапывала себе яму в воде, теряла выталкивание и
            // падала на дно.
            // Расступается сыпучее только под тем, кто тяжелее его,
            // или от удара на скорости. Иначе любое тело просто
            // прокапывает себе ход вниз.
            const double vx2 = p.x - p.ox, vy2 = p.y - p.oy;
            const double hit = std::sqrt(vx2 * vx2 + vy2 * vy2);
            const bool heavy = p.dens > w.dens[o];
            if (st == POWDER && (heavy || hit > 1.2)) {
                // Ищем свободное место не только впритык: внутри кучи
                // песка соседние клетки все заняты, и предмет застревал.
                const int dd[12][2] = {{0,-1},{1,0},{-1,0},{0,1},
                                       {1,-1},{-1,-1},{1,1},{-1,1},
                                       {0,-2},{2,0},{-2,0},{0,2}};
                for (auto& d : dd) {
                    const int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    if (w.at(nx, ny) >= 0) continue;
                    const int t = w.type[o];
                    const double tp = w.tmp[o];
                    w.killAt(cx, cy);
                    const int k = w.create(nx, ny, t);
                    if (k >= 0) w.tmp[k] = tp;
                    w.wakeCell(nx, ny);
                    break;
                }
            }
        }
    }
}

}  // namespace upt
