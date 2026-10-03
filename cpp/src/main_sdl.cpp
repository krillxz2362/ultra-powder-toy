// ULTRA POWDER TOY — окно, ввод, цикл. Ядро про экран ничего не знает:
// оно только заполняет кадр, а здесь кадр попадает в окно.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "upt_core.h"
#include "upt_ui.h"

using namespace upt;

// Избранные вещества для нижней панели: всё до первого элемента
// таблицы Менделеева. Элементов 118, им место в отдельном экране.
static std::vector<int> favourites() {
    std::vector<int> out;
    out.push_back(0);               // ЛАСТИК — первой ячейкой, как в LÖVE
    for (int i = 1; i < SUBSTANCE_COUNT; ++i) {
        if (SUBSTANCES[i].z != 0) break;
        out.push_back(i);
    }
    return out;
}

static int idOf(const char* key) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::strcmp(SUBSTANCES[i].key, key) == 0) return i;
    return 0;
}

struct App {
    int scale = 3;                  // одна клетка мира — столько точек экрана
    int screenW = 0, screenH = 0;
    World* world = nullptr;
    Ui ui;
    std::vector<uint8_t> frame;     // кадр мира, RGBA
    std::vector<uint8_t> panel;     // панель, RGBA в точках экрана

    // Кисть: кладём вещество кругом, как в LÖVE.
    void paint(int wx, int wy, int mat, int r) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r) continue;
                const int x = wx + dx, y = wy + dy;
                if (x < 0 || y < 0 || x >= world->width() || y >= world->height()) continue;
                if (mat == 0) {         // ластик
                    world->killAt(x, y);
                    world->wakeCell(x, y);
                } else if (world->at(x, y) < 0) {
                    world->create(x, y, mat, world->rng.next(256) - 1);
                }
            }
    }
};

int main(int argc, char** argv) {
    int worldW = 320, worldH = 200, scale = 3, startView = 0;
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--снимок" || a == "--shot") headless = true;
        else if (a == "--мир" && i + 2 < argc) { worldW = std::atoi(argv[i+1]); worldH = std::atoi(argv[i+2]); i += 2; }
        else if (a == "--масштаб" && i + 1 < argc) { scale = std::atoi(argv[++i]); }
        else if (a == "--вид" && i + 1 < argc) { startView = std::atoi(argv[++i]); }
    }
    if (headless) SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("не поднялся SDL: %s\n", SDL_GetError());
        return 1;
    }

    App app;
    app.scale = scale;
    World world(worldW, worldH);
    world.rng.seed(2026);
    app.world = &world;

    Ui& ui = app.ui;
    const std::vector<int> favs = favourites();
    ui.selected = idOf("SAND");
    ui.view = startView & 3;

    // Экран: мир сверху, панель снизу. Высоту панели знает только она
    // сама, поэтому сначала раскладываем её, потом считаем окно.
    app.screenW = worldW * scale;
    int guess = worldH * scale;
    ui.layout(app.screenW, guess + 200, favs);
    app.screenH = guess + ui.panelH();
    ui.layout(app.screenW, app.screenH, favs);

    SDL_Window* win = SDL_CreateWindow("ULTRA POWDER TOY",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        app.screenW, app.screenH, SDL_WINDOW_SHOWN);
    if (!win) { std::printf("не вышло окно: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

    SDL_Texture* texWorld = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING, worldW, worldH);
    SDL_Texture* texPanel = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING, app.screenW, app.screenH);
    SDL_SetTextureBlendMode(texPanel, SDL_BLENDMODE_BLEND);

    app.frame.assign(static_cast<size_t>(worldW) * worldH * 4, 0);
    app.panel.assign(static_cast<size_t>(app.screenW) * app.screenH * 4, 0);

    // стартовая сцена: пол и немного воды, чтобы окно не было пустым
    const int STONE = idOf("STONE"), WATER = idOf("WATER");
    for (int x = 0; x < worldW; ++x) {
        world.create(x, worldH - 1, STONE, world.rng.next(256) - 1);
        world.create(x, worldH - 2, STONE, world.rng.next(256) - 1);
    }
    for (int y = worldH - 30; y < worldH - 2; ++y)
        for (int x = 10; x < worldW / 3; ++x)
            world.create(x, y, WATER, world.rng.next(256) - 1);

    bool running = true;
    bool drawing = false, erasing = false;
    int stepOnce = 0;
    uint32_t lastTitle = SDL_GetTicks();
    double msAvg = 0.0;
    int frames = 0;
    const int maxFrames = headless ? 240 : -1;
    int frameNo = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE:  ui.paused = !ui.paused; break;
                    case SDLK_PERIOD: stepOnce = 1; break;
                    case SDLK_LEFTBRACKET:  ui.brush = std::max(1, ui.brush - 1); break;
                    case SDLK_RIGHTBRACKET: ui.brush = std::min(40, ui.brush + 1); break;
                    default: break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                const bool down = (e.type == SDL_MOUSEBUTTONDOWN);
                const int mx = e.button.x, my = e.button.y;
                if (down && my >= ui.y0()) {
                    const Button* b = ui.hit(mx, my);
                    if (b) {
                        switch (b->act) {
                            case Act::Material: ui.selected = b->id; break;
                            case Act::Pause:    ui.paused = !ui.paused; break;
                            case Act::Step:     stepOnce = 1; ui.paused = true; break;
                            case Act::BrushDown: ui.brush = std::max(1, ui.brush - 1); break;
                            case Act::BrushUp:   ui.brush = std::min(40, ui.brush + 1); break;
                            case Act::View:     ui.view = (ui.view + 1) & 3; break;
                            case Act::Arrows:   ui.arrows = !ui.arrows; break;
                            case Act::Clear:    world.clearWorld(); break;
                            case Act::Chem:     break;   // экран химии — следующим шагом
                            default: break;
                        }
                    }
                } else {
                    if (e.button.button == SDL_BUTTON_LEFT)  drawing = down;
                    if (e.button.button == SDL_BUTTON_RIGHT) erasing = down;
                }
                if (!down) { drawing = false; erasing = false; }
            } else if (e.type == SDL_MOUSEWHEEL) {
                ui.brush = std::clamp(ui.brush + e.wheel.y, 1, 40);
            }
        }

        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if ((drawing || erasing) && my < ui.y0()) {
            app.paint(mx / scale, my / scale, erasing ? 0 : ui.selected, ui.brush);
        }

        const uint32_t t0 = SDL_GetTicks();
        if (!ui.paused || stepOnce) {
            world.step();
            if (stepOnce) stepOnce = 0;
        }
        const uint32_t t1 = SDL_GetTicks();
        msAvg = msAvg * 0.9 + (t1 - t0) * 0.1;

        world.fillFrame(app.frame.data(), ui.view);
        SDL_UpdateTexture(texWorld, nullptr, app.frame.data(), worldW * 4);

        // панель поверх: рисуем в свой буфер, прозрачный над миром
        std::fill(app.panel.begin(), app.panel.end(), 0);
        Canvas cv{app.panel.data(), app.screenW, app.screenH};
        int alive = 0;
        for (int i = 0; i < world.maxUsed; ++i) if (world.alive[i]) ++alive;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s  КИСТЬ %d  ЧАСТИЦ %d  ШАГ %.1f МС",
                      SUBSTANCES[ui.selected].name, ui.brush, alive, msAvg);
        ui.status = buf;
        ui.draw(cv);
        SDL_UpdateTexture(texPanel, nullptr, app.panel.data(), app.screenW * 4);

        SDL_Rect dst{0, 0, app.screenW, ui.y0()};
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, texWorld, nullptr, &dst);
        SDL_RenderCopy(ren, texPanel, nullptr, nullptr);
        SDL_RenderPresent(ren);

        ++frames; ++frameNo;
        if (SDL_GetTicks() - lastTitle > 500) {
            lastTitle = SDL_GetTicks();
            char title[128];
            std::snprintf(title, sizeof title, "ULTRA POWDER TOY — шаг %.1f мс, частиц %d",
                          msAvg, alive);
            SDL_SetWindowTitle(win, title);
        }

        if (maxFrames > 0 && frameNo >= maxFrames) {
            // безэкранный прогон: снять картинку и выйти
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, app.screenW, app.screenH,
                                                               32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 shot->pixels, shot->pitch);
            SDL_SaveBMP(shot, "/tmp/upt_окно.bmp");
            SDL_FreeSurface(shot);
            running = false;
        }
    }

    SDL_DestroyTexture(texWorld);
    SDL_DestroyTexture(texPanel);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
