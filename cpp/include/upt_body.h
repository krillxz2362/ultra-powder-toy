// МОДУЛЬ 8: тела — точки, связи, человечки.
//
// Тело сделано из того же, из чего мир: его точки сталкиваются с теми
// же клетками, что и песчинки. Второго движка нет и не будет —
// см. ТЕЛА.md, там объяснено почему.
//
// Движение по Верле: вместо скорости хранится прошлое положение.
// Скорость тогда равна разнице между нынешним и прошлым местом, и её
// невозможно рассогласовать с положением — отсюда устойчивость при
// жёстких связях.
#pragma once
#include <cstdint>
#include <vector>

namespace upt {

class World;

struct BodyPoint {
    double x = 0, y = 0;        // где сейчас
    double ox = 0, oy = 0;      // где был на прошлом шаге
    double r = 0.6;             // радиус, в клетках
    double mass = 1.0;
    // Плотность, кг/м3 — та же мерка, что у веществ. Человек чуть
    // легче воды, оттого и плавает; камень потонет, пробка всплывёт.
    double dens = 900.0;
    uint8_t pinned = 0;         // прибита к миру
    int32_t body = 0;           // к какому телу принадлежит
};

// Связь держит расстояние между двумя точками.
struct Link {
    int32_t a = 0, b = 0;
    double  rest = 1.0;         // нужное расстояние
    double  stiff = 1.0;        // 1 — жёстко, меньше — податливо
};

// Угловой предел: не даёт суставу выворачиваться назад. Хранится как
// минимальное расстояние между крайними точками сустава — так проще и
// устойчивее, чем считать углы.
struct AngleLimit {
    int32_t a = 0, c = 0;       // крайние точки (b — сустав между ними)
    double  minDist = 1.0;
};

class Bodies {
public:
    std::vector<BodyPoint> pts;
    std::vector<Link>      links;
    std::vector<AngleLimit> limits;

    int  iterations = 6;        // проходов решателя связей за шаг
    int  nextBody = 1;

    void clear();

    int  addPoint(double x, double y, double r = 0.6, double mass = 1.0,
                  bool pinned = false, int body = 0);
    void addLink(int a, int b, double stiff = 1.0);
    void addLimit(int a, int c, double minDist);

    // Верёвка: цепочка точек, первая прибита.
    int  makeRope(double x, double y, int n, double seg = 1.2);
    // Человечек: восемь точек, связи и угловые пределы.
    int  makeDoll(double x, double y, double scale = 1.0);

    // Один шаг: тяжесть, связи, столкновение с миром, взаимодействие
    // с веществами.
    void step(World& w);

    // Рука: схватить ближайшую точку и тащить к месту.
    int  grab(double x, double y, double maxDist = 3.0);
    void dragTo(int point, double x, double y);
    void release(int point);

    // Сколько точек у тела под жидкостью — для проверок.
    int  submerged(const World& w, int body) const;
};

}  // namespace upt
