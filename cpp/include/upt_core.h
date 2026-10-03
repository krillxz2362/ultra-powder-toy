// upt_core.h — ядро ULTRAFIZ 2.0 на C++.
//
// Перенос с версии на Lua (game/fiz/sim.lua). Правило переноса: пока
// фаза не сошлась с эталоном число в число, она считается непереехавшей.
// Поэтому порядок действий и формулы повторяют оригинал буквально,
// включая порядок сложения — иначе расхождение в последнем знаке
// накапливается за сотни шагов и ловить его потом невозможно.
//
// Графики здесь нет и не будет: ядро укладывает цвета в чужой буфер,
// а кто его завёл — SDL, LOVE или тест в терминале — ядру неизвестно.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "upt_substances.h"

namespace upt {

// --- постоянные, одинаковые с Lua-эталоном ---------------------------
inline constexpr int    CHUNK      = 16;
inline constexpr int    CSHIFT     = 4;    // CHUNK = 1 << CSHIFT
inline constexpr int    CMASK      = 15;
inline constexpr int    AWAKE      = 6;    // на сколько кадров просыпается кусок
inline constexpr int    AIR_CELL   = 4;
inline constexpr int    ASHIFT     = 2;    // AIR_CELL = 1 << ASHIFT

inline constexpr double ROOM       = 22.0;
inline constexpr double TMIN       = -273.0;
inline constexpr double TMAX       = 3500.0;

inline constexpr double FLOW       = 0.12;   // доля теплообмена за шаг
inline constexpr double BODY_RELAX = 0.0015; // медленный уход к комнатной
inline constexpr double AIR_COND   = 0.15;   // предел обмена с воздухом
inline constexpr double CAIR       = 1.0;    // теплоёмкость ячейки воздуха

inline constexpr double GRAV       = 0.14;
inline constexpr double MAXV       = 6.0;
inline constexpr double SWAP_MIN   = 0.002;  // порог разницы плотностей для обмена
inline constexpr double PRESS_K    = 0.30;
inline constexpr double PRESS_MAX  = 1.2;
inline constexpr int    GAP_MAX    = 2;      // пузырь такой высоты не разрывает столб
inline constexpr double T0ABS      = 295.0;
inline constexpr double THERMAL_GAIN = 30.0;

// Генератор случайных чисел (xorshift32). Он же, число в число, стоит в
// Lua-эталоне: без общей последовательности сравнить поведение двух
// версий невозможно.
class Rng {
public:
    void seed(uint32_t v) { s_ = v ? v : 2463534242u; }
    double next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_ * (1.0 / 4294967296.0);
    }
    int next(int n) { return static_cast<int>(next() * n) + 1; }
private:
    uint32_t s_ = 2463534242u;
};

// --- поле воздуха ----------------------------------------------------
// Огрублённая сетка: одна ячейка на 4x4 клетки мира. Воздух меняется
// плавно, и полная сетка стоила бы вчетверо дороже без видимой разницы.
inline constexpr int    AIR_PMAX   = 256.0;
inline constexpr double TSTEPP = 0.30;   // давление -> скорость
inline constexpr double TSTEPV = 0.40;   // скорость -> давление
inline constexpr double ADV    = 0.30;   // перенос потока самим собой
inline constexpr double PADV   = 0.25;   // перенос давления потоком
inline constexpr double SADV   = 0.60;   // перенос примесей
inline constexpr double VLOSS  = 0.994;
inline constexpr double PLOSS  = 0.992;
inline constexpr double BUOY   = 0.00075;
inline constexpr double TRELAX = 0.004;
inline constexpr double TDIFF  = 0.14;
inline constexpr double ODIFF  = 0.10;

struct Air {
    int cw = 0, chh = 0, n = 0;
    std::vector<double> at;   // температура
    std::vector<double> pv;   // давление
    std::vector<double> vx, vy;
    std::vector<double> ox;   // кислород
    std::vector<double> opv, ovx, ovy, oat, oox;  // копии на время переноса
    std::vector<double> wall;   // доля ячейки, занятая веществом
    std::vector<double> adens;  // плотность воздуха с поправкой на температуру

    void init(int w, int h);
    void clear();
    void update();
    void blast(int x, int y, double power);
    int  index(int x, int y) const {
        int cx = x / AIR_CELL, cy = y / AIR_CELL;
        if (cx < 0) cx = 0; else if (cx > cw - 1)  cx = cw - 1;
        if (cy < 0) cy = 0; else if (cy > chh - 1) cy = chh - 1;
        return cy * cw + cx;
    }
};

// --- мир -------------------------------------------------------------
class World {
public:
    World(int w, int h);

    int width()  const { return w_; }
    int height() const { return h_; }

    // Создать частицу. Возвращает её номер или -1, если клетка занята
    // либо место кончилось.
    int  create(int x, int y, int type, int rnd = -1);
    void killIndex(int i);
    void killAt(int x, int y);
    void convert(int i, int type);

    // Номер частицы в клетке или -1.
    int  at(int x, int y) const {
        int32_t v = pmap_[static_cast<size_t>(y) * w_ + x];
        return v == 0 ? -1 : v - 1;
    }

    void wakeCell(int x, int y);
    void wakeChunk(int cx, int cy);
    void wakeChunkArea(int cx, int cy);
    // Ставит метку в куске клетки и в соседях, которых она касается.
    // Одна на всех: и пробуждение движения, и тепловая метка.
    void markAt(std::vector<uint8_t>& map, int x, int y);

    // Фазы шага. Каждая перенесена с Lua один в один и сверена.
    // Опора. Проход по столбцам снизу вверх: частица опёрта, если под
    // ней неподвижное тело, край мира или другая опёртая частица. Это
    // цепочка контактов до земли — ровно то, что в жизни передаёт вес.
    // Неопёртая частица находится в свободном падении, и никакая сила
    // реакции на неё не действует, даже если клетка под ней занята.
    void support();
    void liquidPressure();  // вес столба + общий уровень связных областей
    void heat();
    void densities();   // плотность с поправкой на температуру
    void forces();      // силы: тяжесть, Архимед, натяжение, вязкость, ветер
    void advect();      // перенос частиц, столкновения, скольжение
    void move() { forces(); advect(); }
    void react();       // горение, фазовые переходы, кислота, растворение
    void mix();         // химические реакции пар и разложение
    void couple();      // частицы тормозят и греют воздух
    void clampWall();
    void explode(int x, int y, double power);
    // Очистить мир: убрать все частицы и успокоить воздух.
    void clearWorld();
    void step();        // один шаг мира: все фазы в своём порядке

    // Заполнить кадр. Буфер — RGBA по четыре байта на клетку, ширина на
    // высоту мира; выделяет его вызывающий, ядро про экран ничего не
    // знает. Ровно те же правила цвета, что в эталоне: цвет вещества,
    // оттенок по полю shd, фон у пустоты.
    // Режимы показа: вещество, тепло, давление, кислород. Те же
    // цветовые шкалы, что в эталоне на LÖVE.
    enum View { VIEW_MAT = 0, VIEW_HEAT = 1, VIEW_PRES = 2, VIEW_OXY = 3 };
    void fillFrame(uint8_t* rgba, int view = VIEW_MAT) const;

    Rng rng;

    // --- данные частиц (SoA, как в Lua) ---
    std::vector<uint16_t> type;
    std::vector<uint8_t>  alive;
    std::vector<double>   px, py;
    std::vector<double>   vx, vy;
    std::vector<double>   tmp;      // температура
    std::vector<double>   tmp2;     // буфер на время обмена
    std::vector<uint16_t> residue;
    std::vector<int32_t>  life;     // остаток срока жизни
    std::vector<double>   lat;      // набранная скрытая теплота
    std::vector<int32_t>  burn;     // сколько шагов ещё идёт горение
    std::vector<double>   burnT;    // температура горения
    std::vector<uint8_t>  shd;      // оттенок, для отрисовки
    std::vector<double>   dens;     // плотность с поправкой на температуру
    std::vector<uint8_t>  dir;      // куда пробовать соскользнуть первым
    std::vector<uint8_t>  settled;  // сыпучее улеглось и держит склон
    std::vector<double>   head;     // избыток напора над общим уровнем
    std::vector<uint8_t>  tl;       // сколько соседей того же вещества
    std::vector<uint8_t>  held;     // опёрта ли частица (цепочка до земли)
    std::vector<int32_t> order_;   // порядок обхода в advect, снизу вверх
    std::vector<size_t>  rowCnt_;  // счётчики рядов для этого порядка
    std::vector<double>   sx, sy;   // шаг подшага
    std::vector<double>   hit;      // сила удара на этом шаге

    // Поля гидростатики.
    // Целое, а не дробное: в эталоне это int32_t, и половина плотности
    // там отбрасывается. На воде (1000) это незаметно, на спирте (789)
    // половина уже не целая — и значения расходятся.
    std::vector<int32_t>  lp;       // давление столба в клетке
    std::vector<int32_t>  ctop;     // общий уровень связной области
    std::vector<int32_t>  coltop;   // уровень своего столба
    std::vector<int32_t>  cstack;   // стопка обхода связной области
    std::vector<int32_t>  clist;    // клетки текущей области
    std::vector<int32_t>  rowcnt;   // сколько клеток поверхности в ряду

    std::vector<int32_t>  pmap_;    // номер частицы в клетке + 1
    std::vector<uint8_t>  awake, therm;

    Air air;
    int maxUsed = 0;
    int count = 0;
    int cw = 0, ch = 0, nchunks = 0;
    int acw = 0, ach = 0;

private:
    int w_, h_, maxp_;
    std::vector<int32_t> free_;     // список свободных номеров
    int freeTop_ = 0;
};

// Номера веществ, на которые ядро ссылается по имени. Считаются из
// таблицы на этапе сборки: держать их числами нельзя — при любой правке
// elements.lua номера съедут, а огонь молча станет чем-то другим.
constexpr bool keyEq(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
constexpr int keyId(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (keyEq(SUBSTANCES[i].key, k)) return i;
    return -1;
}
inline constexpr int ID_FIRE  = keyId("FIRE");
inline constexpr int ID_SMOKE = keyId("SMOKE");
inline constexpr int ID_ACID  = keyId("ACID");
inline constexpr int ID_WATER = keyId("WATER");
inline constexpr int ID_STEAM = keyId("STEAM");
inline constexpr int ID_SALT  = keyId("SALT");
inline constexpr int ID_BRINE = keyId("BRINE");
inline constexpr int ID_ICE   = keyId("ICE");
static_assert(ID_FIRE >= 0 && ID_WATER >= 0 && ID_ACID >= 0, "нет опорного вещества");

inline constexpr double PRESS_BOIL = 2.2;
inline constexpr double LAT_SCALE  = 0.20;
inline constexpr double BLAST_K    = 55.0;

// Отрисовка: оттенков на вещество и цвет пустоты.
inline constexpr int     SHADES = 32;
inline constexpr uint8_t BG_R = 18, BG_G = 18, BG_B = 22;
// Палитра: на каждое вещество SHADES оттенков по три байта. Строится
// один раз при первом кадре.
const uint8_t* palette();

// Строит плоскую таблицу пар из разреженного списка (upt_chem.h).
void buildChemTables();
int32_t reactionAt(int a, int b);
int32_t decayAt(int t);
bool    chemReady();

} // namespace upt
