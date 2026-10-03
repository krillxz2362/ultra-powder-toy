// Интерфейс: нижняя панель, кнопки, свой шрифт.
// Рисует прямо в буфер RGBA — ни SDL, ни OpenGL здесь не нужны, чтобы
// интерфейс можно было проверять снимками без окна.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace upt {

struct Color { uint8_t r, g, b; };

// Холст: кусок памяти RGBA, по четыре байта на точку.
struct Canvas {
    uint8_t* px = nullptr;
    int w = 0, h = 0;
    void dot(int x, int y, Color c, uint8_t a = 255);
    void fill(int x, int y, int ww, int hh, Color c, uint8_t a = 255);
    void frame(int x, int y, int ww, int hh, Color c, uint8_t a = 255);
};

// --- шрифт ---
// Ширина строки в точках при данном масштабе (между буквами один пробел).
int textWidth(const std::string& s, int scale);
void drawText(Canvas& cv, int x, int y, const std::string& s, int scale, Color c);

// Стрелки потока: куда и насколько гонит воздух. Рисуются поверх мира,
// в точках экрана, поэтому живут здесь, а не в ядре.
class Air;
// Прямоугольник, в котором показан мир. Один и тот же для картинки,
// стрелок и попадания пальца — иначе они разъезжаются.
struct WorldView {
    int x = 0, y = 0, w = 0, h = 0;     // место на экране, в точках
    int cols = 0, rows = 0;             // размер мира, в клетках
    double sx() const { return cols > 0 ? static_cast<double>(w) / cols : 1.0; }
    double sy() const { return rows > 0 ? static_cast<double>(h) / rows : 1.0; }
    int cellX(int px) const { return static_cast<int>((px - x) / sx()); }
    int cellY(int py) const { return static_cast<int>((py - y) / sy()); }
};

void drawFlow(Canvas& cv, const Air& air, const WorldView& wv);

// --- панель ---
enum class Act {
    None, Material, Pause, Step, BrushDown, BrushUp, View, Arrows, Chem, Clear,
    ChemPick, ChemClose, Save, Load,
    MgrOpen, MgrClose, MgrLoad, MgrDelete,
    TempDown, TempUp, TempAuto, WaterModel
};

struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    Act act = Act::None;
    int id = 0;                 // номер вещества для Act::Material
    std::string label;
    Color color{60, 70, 90};
};

class Ui {
public:
    // Разложить панель под экран заданного размера.
    void layout(int screenW, int screenH, const std::vector<int>& favourites);

    // Что под точкой. Возвращает кнопку или nullptr.
    const Button* hit(int x, int y) const;

    void draw(Canvas& cv) const;

    // Экран ХИМИЯ: таблица Менделеева во весь экран. Раскладка считается
    // один раз на размер экрана, как и панель.
    void layoutChem(int screenW, int screenH);
    const Button* hitChem(int x, int y) const;
    void drawChem(Canvas& cv) const;

    bool chemOpen = false;
    std::vector<Button> chemButtons;

    // Издание. В Base экрана ХИМИЯ нет: там только базовые вещества.
    bool chemAvailable = true;
    // Ветка администратора: лишние возможности, обычной сборке не нужные.
    bool admin = false;

    // Менеджер сохранений — только в ветке администратора.
    void layoutManager(int screenW, int screenH, int count);
    const Button* hitManager(int x, int y) const;
    void drawManager(Canvas& cv, const std::vector<std::string>& lines) const;
    bool mgrOpen = false;
    std::vector<Button> mgrButtons;

    int panelH() const { return panelH_; }
    int y0() const { return y0_; }          // верх панели
    int worldAreaH() const { return y0_; }  // миру остаётся всё, что выше

    // Состояние, которое панель показывает.
    int  selected = 0;       // выбранное вещество
    bool paused = false;
    bool arrows = false;
    int  brush = 4;
    // Температура кисти. «Авто» — вещество появляется со своей
    // обычной температурой; иначе берётся заданная. Так можно зажечь
    // холодный огонь или налить раскалённую воду.
    bool   tempAuto = true;
    double brushTemp = 400.0;
    // ВОДА 2.0: какая из трёх моделей жидкости сейчас считает.
    // 0 — клетки (как было), 1 — доля заполнения, 2 — гибрид с каплями.
    int water = 0;
    int  view = 0;           // 0 вещество, 1 тепло, 2 давление, 3 кислород
    std::string status;      // строка поверх мира

    std::vector<Button> buttons;

private:
    int screenW_ = 0, screenH_ = 0;
    int panelH_ = 0, y0_ = 0, rowH_ = 0, ctrlH_ = 0;
    int cols_ = 7;
};

}  // namespace upt
