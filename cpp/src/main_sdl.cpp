// ULTRA POWDER TOY — окно, ввод, цикл. Ядро про экран ничего не знает:
// оно только заполняет кадр, а здесь кадр попадает в окно.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "upt_core.h"
#include "upt_save.h"
#include "upt_ui.h"

// Версия видна в строке состояния: по снимку с телефона сразу понятно,
// какая сборка запущена.
// Версию передаёт сборщик: -DUPT_VERSION_RAW=1.4.0, без кавычек —
// кавычки по пути через make и ndk-build съедаются, и сборка падает.
#define UPT_STR2(x) #x
#define UPT_STR(x) UPT_STR2(x)
#ifdef UPT_VERSION_RAW
#define UPT_VERSION UPT_STR(UPT_VERSION_RAW)
#else
#define UPT_VERSION "1.B"
#endif

// Издание и ветка задаются при сборке:
//   UPT_BASE   — издание Base: только базовые вещества, без химии
//   UPT_ADMIN  — ветка UptA: менеджер сохранений и прочее служебное
#ifdef UPT_BASE
#define UPT_EDITION "BASE"
#else
#define UPT_EDITION "ALCO"
#endif

using namespace upt;

// Избранные вещества для нижней панели: ластик и всё до первого
// элемента таблицы Менделеева. Элементов 118, им место в экране ХИМИЯ.
static std::vector<int> favourites() {
    std::vector<int> out;
    out.push_back(0);                   // ЛАСТИК
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

int main(int argc, char** argv) {
    int askW = 320, askH = 200, scale = 3, startView = 0;
    bool headless = false, openChem = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--снимок" || a == "--shot") headless = true;
        else if (a == "--мир" && i + 2 < argc) { askW = std::atoi(argv[i+1]); askH = std::atoi(argv[i+2]); i += 2; }
        else if (a == "--масштаб" && i + 1 < argc) { scale = std::atoi(argv[++i]); }
        else if (a == "--вид" && i + 1 < argc) { startView = std::atoi(argv[++i]); }
        else if (a == "--химия") openChem = true;   // для снимка экрана
    }
    if (headless) SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
    // Мир растягивается по целым клеткам — сглаживание тут только мылит.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("не поднялся SDL: %s\n", SDL_GetError());
        return 1;
    }

    Ui ui;
    const std::vector<int> favs = favourites();
    ui.selected = idOf("SAND");
#ifdef UPT_BASE
    ui.chemAvailable = false;       // в Base таблицы Менделеева нет
#endif
#ifdef UPT_ADMIN
    ui.admin = true;
#endif
    ui.view = startView & 3;

    // --- окно ---------------------------------------------------------
    // Размер окна на телефоне задаём не мы. Поэтому сперва окно, и уже
    // от его НАСТОЯЩЕГО размера считаем всё остальное. Раньше раскладка
    // и текстуры считались из заказанного размера: панель потом
    // растягивалась на всё окно, а нажатия проверялись по нерастянутым
    // числам — кнопки не совпадали с тем, что видно.
    uint32_t winFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
    winFlags |= SDL_WINDOW_FULLSCREEN;
#endif
    // На столе просим примерно столько, сколько заказали; точный размер
    // всё равно спросим у окна.
    int wantW = askW * scale, wantH = askH * scale + 140;
    SDL_Window* win = SDL_CreateWindow("ULTRA POWDER TOY",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, wantW, wantH, winFlags);
    if (!win) { std::printf("не вышло окно: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { std::printf("нет рисовалки: %s\n", SDL_GetError()); return 1; }

    // Касания и мышь приходят в координатах окна. Чтобы рисовать в них
    // же, задаём рисовалке те же координаты: тогда «куда ткнул» и «что
    // нарисовано» — одно и то же число, при любой плотности экрана.
    int W = 0, H = 0;
    SDL_GetWindowSize(win, &W, &H);
    if (W <= 0 || H <= 0) SDL_GetRendererOutputSize(ren, &W, &H);
    SDL_RenderSetLogicalSize(ren, W, H);

    // --- раскладка и мир ----------------------------------------------
#ifdef __ANDROID__
    // Клетка должна быть видна пальцу, но мир не должен стать огромным:
    // его считать каждый кадр.
    scale = W / 420;
    scale = std::clamp(scale, 2, 6);
#endif
    ui.layout(W, H, favs);
    ui.layoutChem(W, H);
    ui.chemOpen = openChem;

    int worldW = std::max(40, W / scale);
    int worldH = std::max(40, ui.y0() / scale);   // мир — ровно до панели

    // Один прямоугольник на всё: в него рисуется мир, по нему же
    // считаются стрелки потока и попадание пальца в клетку. Пока это
    // были три отдельных расчёта, они разъезжались.
    WorldView wv;

    auto world = std::make_unique<World>(worldW, worldH);
    world->rng.seed(2026);

    SDL_Texture* texWorld = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING, worldW, worldH);
    SDL_Texture* texPanel = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureBlendMode(texPanel, SDL_BLENDMODE_BLEND);

    std::vector<uint8_t> frame(static_cast<size_t>(worldW) * worldH * 4, 0);
    std::vector<uint8_t> panel(static_cast<size_t>(W) * H * 4, 0);

    // стартовая сцена: пол и немного воды, чтобы окно не было пустым
    const int STONE = idOf("STONE"), WATER = idOf("WATER");
    auto scene = [&]() {
        for (int x = 0; x < worldW; ++x) {
            world->create(x, worldH - 1, STONE, world->rng.next(256) - 1);
            world->create(x, worldH - 2, STONE, world->rng.next(256) - 1);
        }
        const int top = std::max(2, worldH - 30);
        for (int y = top; y < worldH - 2; ++y)
            for (int x = 10; x < worldW / 3; ++x)
                world->create(x, y, WATER, world->rng.next(256) - 1);
    };
    scene();

    // Кисть: кладём вещество кругом.
    auto paint = [&](int wx, int wy, int mat, int r) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r) continue;
                const int x = wx + dx, y = wy + dy;
                if (x < 0 || y < 0 || x >= worldW || y >= worldH) continue;
                if (mat == 0) {
                    world->killAt(x, y);
                    world->wakeCell(x, y);
                } else if (world->at(x, y) < 0) {
                    const int k = world->create(x, y, mat, world->rng.next(256) - 1);
                    // Заданная температура кисти: так зажигают холодный
                    // огонь или наливают раскалённую воду.
                    if (k >= 0 && !ui.tempAuto) world->tmp[k] = ui.brushTemp;
                }
            }
    };

    // Сохранения лежат в папке приложения: на телефоне она внутренняя
    // и в файловом менеджере не видна.
    const std::string saveDir = savesDir();
    const std::string slot1 = saveDir + "слот1.upt";
    std::vector<SaveEntry> saves;     // список для менеджера
    std::vector<std::string> saveLines;
    auto refreshSaves = [&]() {
        saves = listSaves();
        saveLines.clear();
        for (const SaveEntry& e : saves) {
            char l[200];
            if (e.readable) {
                std::snprintf(l, sizeof l, "%s   %d ЧАСТИЦ   %dX%d   %llu КБ",
                              e.name.c_str(), e.info.particles,
                              e.info.width, e.info.height,
                              static_cast<unsigned long long>(e.bytes / 1024));
            } else {
                std::snprintf(l, sizeof l, "%s   ИСПОРЧЕНО", e.name.c_str());
            }
            saveLines.push_back(l);
        }
        ui.layoutManager(W, H, static_cast<int>(saves.size()));
    };

    std::string note;                 // короткое сообщение поверх мира
    uint32_t noteUntil = 0;

    bool running = true;
    bool drawing = false, erasing = false;
    int stepOnce = 0;
    uint32_t lastTitle = SDL_GetTicks();
    double msAvg = 0.0;
    const int maxFrames = headless ? 240 : -1;
    int frameNo = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_APP_WILLENTERBACKGROUND) {
                // Свернули — считать мир незачем, да и батарею жалко.
                ui.paused = true;
            }
            else if (e.type == SDL_WINDOWEVENT &&
                     (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      e.window.event == SDL_WINDOWEVENT_RESIZED)) {
                // Размер окна поменялся (поворот, разделённый экран).
                // Панель и её кнопки обязаны пересчитаться, иначе
                // нажатия снова разъедутся с картинкой.
                SDL_GetWindowSize(win, &W, &H);
                if (W > 0 && H > 0) {
                    SDL_RenderSetLogicalSize(ren, W, H);
                    ui.layout(W, H, favs);
                    ui.layoutChem(W, H);
                    ui.layoutManager(W, H, static_cast<int>(saves.size()));
                    panel.assign(static_cast<size_t>(W) * H * 4, 0);
                    SDL_DestroyTexture(texPanel);
                    texPanel = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                        SDL_TEXTUREACCESS_STREAMING, W, H);
                    SDL_SetTextureBlendMode(texPanel, SDL_BLENDMODE_BLEND);

                    // Мир тоже считается от размера окна. На телефоне
                    // настоящий размер нередко приходит уже после того,
                    // как окно создано, — без этого мир остался бы с
                    // первым, неверным размером и не закрыл бы экран.
                    const int nw = std::max(40, W / scale);
                    const int nh = std::max(40, ui.y0() / scale);
                    if (std::abs(nw - worldW) > 2 || std::abs(nh - worldH) > 2) {
                        worldW = nw; worldH = nh;
                        world = std::make_unique<World>(worldW, worldH);
                        world->rng.seed(2026);
                        SDL_DestroyTexture(texWorld);
                        texWorld = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                            SDL_TEXTUREACCESS_STREAMING, worldW, worldH);
                        frame.assign(static_cast<size_t>(worldW) * worldH * 4, 0);
                        scene();
                    }
                }
            }
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_AC_BACK) {
                // «Назад» на телефоне: сперва закрывает химию и только
                // потом выходит из игры.
                if (ui.mgrOpen) ui.mgrOpen = false;
                else if (ui.chemOpen) ui.chemOpen = false;
                else running = false;
            }
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE:  ui.paused = !ui.paused; break;
                    case SDLK_PERIOD: stepOnce = 1; break;
                    case SDLK_LEFTBRACKET:  ui.brush = std::max(1, ui.brush - 1); break;
                    case SDLK_RIGHTBRACKET: ui.brush = std::min(40, ui.brush + 1); break;
                    default: break;
                }
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                const bool down = (e.type == SDL_MOUSEBUTTONDOWN);
                const int mx = e.button.x, my = e.button.y;
                if (ui.mgrOpen) {
                    if (down) {
                        const Button* b = ui.hitManager(mx, my);
                        if (b && b->act == Act::MgrClose) ui.mgrOpen = false;
                        else if (b && b->act == Act::MgrLoad &&
                                 b->id < static_cast<int>(saves.size())) {
                            std::string err; int lost = 0;
                            note = loadWorld(*world, saves[b->id].path, err, &lost)
                                 ? (lost ? "ЗАГРУЖЕНО, ПРОПУЩЕНО " + std::to_string(lost)
                                         : "ЗАГРУЖЕНО")
                                 : ("НЕ ЗАГРУЗИЛОСЬ: " + err);
                            noteUntil = SDL_GetTicks() + 2500;
                            ui.mgrOpen = false;
                        }
                        else if (b && b->act == Act::MgrDelete &&
                                 b->id < static_cast<int>(saves.size())) {
                            note = removeSave(saves[b->id].path) ? "УДАЛЕНО"
                                                                 : "НЕ УДАЛИЛОСЬ";
                            noteUntil = SDL_GetTicks() + 2000;
                            refreshSaves();
                        }
                    }
                } else if (ui.chemOpen) {
                    // Пока открыта химия, мир не трогаем: экран закрывает
                    // его целиком, и мазок вслепую не нужен.
                    if (down) {
                        const Button* b = ui.hitChem(mx, my);
                        if (b) {
                            if (b->act == Act::ChemClose) ui.chemOpen = false;
                            else if (b->act == Act::ChemPick && b->id > 0) {
                                ui.selected = b->id;
                                ui.chemOpen = false;
                            }
                        }
                    }
                } else if (down && my >= ui.y0()) {
                    const Button* b = ui.hit(mx, my);
                    if (b) {
                        switch (b->act) {
                            case Act::Material: ui.selected = b->id; break;
                            case Act::Pause:    ui.paused = !ui.paused; break;
                            case Act::Step:     stepOnce = 1; ui.paused = true; break;
                            case Act::BrushDown: ui.brush = std::max(1, ui.brush - 1); break;
                            case Act::BrushUp:   ui.brush = std::min(40, ui.brush + 1); break;
                            case Act::TempAuto:  ui.tempAuto = !ui.tempAuto; break;
                            case Act::WaterModel: {
                                // Три модели воды по кругу. При
                                // переходе на новые доля заполнения
                                // ставится в единицу: клетка занята
                                // целиком, как было до переключения.
                                ui.water = (ui.water + 1) % 3;
                                static const int mm[3] = {World::LIQ_CELLS,
                                                          World::LIQ_FILL,
                                                          World::LIQ_HYBRID};
                                world->liquidModel = mm[ui.water];
                                world->drops.clear();
                                world->fill.assign(world->maxp(), 1.0);
                                note = (ui.water == 0) ? "ВОДА: КЛЕТКИ"
                                     : (ui.water == 1) ? "ВОДА: ДОЛЯ ЗАПОЛНЕНИЯ"
                                                       : "ВОДА: ГИБРИД С КАПЛЯМИ";
                                noteUntil = SDL_GetTicks() + 2500;
                                break;
                            }
                            case Act::TempDown:
                            case Act::TempUp: {
                                // Шаг крупнее на больших значениях: от
                                // нуля до трёх тысяч по десятке — это
                                // триста нажатий.
                                ui.tempAuto = false;
                                const double a = std::fabs(ui.brushTemp);
                                const double st = (a < 100) ? 10 : (a < 1000 ? 50 : 100);
                                ui.brushTemp += (b->act == Act::TempUp) ? st : -st;
                                ui.brushTemp = std::clamp(ui.brushTemp, -273.0, 3500.0);
                                break;
                            }
                            case Act::View:     ui.view = (ui.view + 1) & 3; break;
                            case Act::Arrows:   ui.arrows = !ui.arrows; break;
                            case Act::Clear:    world->clearWorld(); break;
                            case Act::Save: {
                                std::string err;
                                note = saveWorld(*world, slot1, err)
                                     ? "СОХРАНЕНО" : ("НЕ СОХРАНИЛОСЬ: " + err);
                                noteUntil = SDL_GetTicks() + 2500;
                                break;
                            }
                            case Act::Load: {
                                std::string err;
                                int lost = 0;
                                if (loadWorld(*world, slot1, err, &lost)) {
                                    note = lost ? ("ЗАГРУЖЕНО, ПРОПУЩЕНО " + std::to_string(lost))
                                                : "ЗАГРУЖЕНО";
                                } else {
                                    note = "НЕ ЗАГРУЗИЛОСЬ: " + err;
                                }
                                noteUntil = SDL_GetTicks() + 2500;
                                break;
                            }
                            case Act::Chem:     ui.chemOpen = true; break;
                            case Act::MgrOpen:  refreshSaves(); ui.mgrOpen = true; break;
                            default: break;
                        }
                    }
                } else {
                    if (e.button.button == SDL_BUTTON_LEFT)  drawing = down;
                    if (e.button.button == SDL_BUTTON_RIGHT) erasing = down;
                }
                if (!down) { drawing = false; erasing = false; }
            }
            else if (e.type == SDL_MOUSEWHEEL) {
                ui.brush = std::clamp(ui.brush + e.wheel.y, 1, 40);
            }
        }

        wv.x = 0; wv.y = 0;
        wv.w = W; wv.h = ui.y0();
        wv.cols = worldW; wv.rows = worldH;

        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if ((drawing || erasing) && !ui.chemOpen && !ui.mgrOpen && my < ui.y0()) {
            paint(wv.cellX(mx), wv.cellY(my), erasing ? 0 : ui.selected, ui.brush);
        }

        const uint32_t t0 = SDL_GetTicks();
        if (!ui.paused || stepOnce) {
            world->step();
            if (stepOnce) stepOnce = 0;
        }
        msAvg = msAvg * 0.9 + (SDL_GetTicks() - t0) * 0.1;

        world->fillFrame(frame.data(), ui.view);
        SDL_UpdateTexture(texWorld, nullptr, frame.data(), worldW * 4);

        std::fill(panel.begin(), panel.end(), 0);
        Canvas cv{panel.data(), W, H};
        int alive = 0;
        for (int i = 0; i < world->maxUsed; ++i) if (world->alive[i]) ++alive;
        char buf[160];
        std::snprintf(buf, sizeof buf,
            "%s  КИСТЬ %d  ЧАСТИЦ %d  ШАГ %.1f МС  | %s %s%s ОКНО %dX%d МИР %dX%d X%d",
            SUBSTANCES[ui.selected].name, ui.brush, alive, msAvg,
            UPT_VERSION, UPT_EDITION, ui.admin ? " UPTA" : "",
            W, H, worldW, worldH, scale);
        ui.status = buf;
        if (!note.empty()) {
            if (SDL_GetTicks() < noteUntil) ui.status = note + "   " + ui.status;
            else note.clear();
        }
        // Стрелки потока поверх мира: в режиме давления всегда, иначе
        // по кнопке ПОТОК.
        if (!ui.chemOpen && !ui.mgrOpen && (ui.arrows || ui.view == World::VIEW_PRES))
            drawFlow(cv, world->air, wv);
        if (ui.mgrOpen)       ui.drawManager(cv, saveLines);
        else if (ui.chemOpen) ui.drawChem(cv);
        else                  ui.draw(cv);
        SDL_UpdateTexture(texPanel, nullptr, panel.data(), W * 4);

        // Мир занимает всё место над панелью — ровно то, из которого
        // считался его размер.
        SDL_Rect dst{wv.x, wv.y, wv.w, wv.h};
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, texWorld, nullptr, &dst);
        SDL_RenderCopy(ren, texPanel, nullptr, nullptr);
        SDL_RenderPresent(ren);

        ++frameNo;
        if (SDL_GetTicks() - lastTitle > 500) {
            lastTitle = SDL_GetTicks();
            char title[128];
            std::snprintf(title, sizeof title, "ULTRA POWDER TOY — шаг %.1f мс, частиц %d",
                          msAvg, alive);
            SDL_SetWindowTitle(win, title);
        }

        if (maxFrames > 0 && frameNo >= maxFrames) {
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32,
                                                               SDL_PIXELFORMAT_ARGB8888);
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
