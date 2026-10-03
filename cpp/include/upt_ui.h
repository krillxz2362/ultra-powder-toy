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

// --- панель ---
enum class Act {
    None, Material, Pause, Step, BrushDown, BrushUp, View, Arrows, Chem, Clear
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

    int panelH() const { return panelH_; }
    int y0() const { return y0_; }          // верх панели
    int worldAreaH() const { return y0_; }  // миру остаётся всё, что выше

    // Состояние, которое панель показывает.
    int  selected = 0;       // выбранное вещество
    bool paused = false;
    bool arrows = false;
    int  brush = 4;
    int  view = 0;           // 0 вещество, 1 тепло, 2 давление, 3 кислород
    std::string status;      // строка поверх мира

    std::vector<Button> buttons;

private:
    int screenW_ = 0, screenH_ = 0;
    int panelH_ = 0, y0_ = 0, rowH_ = 0, ctrlH_ = 0;
    int cols_ = 7;
};

}  // namespace upt
