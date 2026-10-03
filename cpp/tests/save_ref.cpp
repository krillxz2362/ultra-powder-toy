// Сохранения: запись, чтение, устойчивость к смене номеров веществ.
#include "upt_core.h"
#include "upt_save.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace upt;
static int fails = 0;
static void ok(bool c, const char* what, const std::string& got = "") {
    std::printf("%s %s %s\n", c ? "[ОК]  " : "[ПЛОХО]", what, got.c_str());
    if (!c) ++fails;
}
static int ID(const char* k) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (std::strcmp(SUBSTANCES[i].key, k) == 0) return i;
    return -1;
}

int main() {
    const std::string path = "/tmp/проверка_сохранения.upt";
    const int W = 90, H = 70;
    const int SAND = ID("SAND"), WATER = ID("WATER"), STONE = ID("STONE"),
              OIL = ID("OIL"), FIRE = ID("FIRE");

    // Сцена: пол, куча песка, лужа, масло и огонь — чтобы в палитру
    // попало несколько веществ, а частицы имели разные поля.
    World a(W, H);
    a.rng.seed(1234);
    for (int x = 0; x < W; ++x) a.create(x, H - 1, STONE, a.rng.next(256) - 1);
    for (int y = 40; y < 60; ++y) for (int x = 5; x < 40; ++x) a.create(x, y, SAND, a.rng.next(256) - 1);
    for (int y = 50; y < 60; ++y) for (int x = 45; x < 80; ++x) a.create(x, y, WATER, a.rng.next(256) - 1);
    for (int y = 30; y < 36; ++y) for (int x = 50; x < 70; ++x) a.create(x, y, OIL, a.rng.next(256) - 1);
    for (int x = 55; x < 60; ++x) a.create(x, 29, FIRE, a.rng.next(256) - 1);
    for (int i = 0; i < 120; ++i) a.step();

    int before = 0;
    double sumX = 0, sumY = 0, sumT = 0;
    for (int i = 0; i < a.maxUsed; ++i) if (a.alive[i] == 1) {
        ++before; sumX += a.px[i]; sumY += a.py[i]; sumT += a.tmp[i];
    }
    ok(before > 500, "сцена набралась", std::to_string(before) + " частиц");

    std::string err;
    ok(saveWorld(a, path, err), "мир записался", err);

    SaveInfo info;
    ok(readSaveInfo(path, info, err), "заголовок читается без загрузки", err);
    ok(info.particles == before, "в заголовке верное число частиц",
       std::to_string(info.particles));
    ok(info.width == W && info.height == H, "в заголовке верный размер");
    ok(info.substances >= 4, "в палитре несколько веществ",
       std::to_string(info.substances) + " шт.");

    // Загрузка в другой мир.
    World b(W, H);
    int skipped = -1;
    ok(loadWorld(b, path, err, &skipped), "мир прочитался", err);
    ok(skipped == 0, "ничего не потеряно", std::to_string(skipped));

    int after = 0;
    double sumX2 = 0, sumY2 = 0, sumT2 = 0;
    for (int i = 0; i < b.maxUsed; ++i) if (b.alive[i] == 1) {
        ++after; sumX2 += b.px[i]; sumY2 += b.py[i]; sumT2 += b.tmp[i];
    }
    ok(after == before, "частиц столько же", std::to_string(after));
    // float, поэтому сверяем с допуском, а не точно
    ok(std::abs(sumX - sumX2) < 0.5 && std::abs(sumY - sumY2) < 0.5,
       "частицы на своих местах");
    ok(std::abs(sumT - sumT2) < 0.5, "температуры сохранились");

    // Вещества совпали поимённо, а не по номерам.
    int sameType = 0, checked = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const int ia = a.at(x, y), ib = b.at(x, y);
        if (ia >= 0 && ib >= 0) { ++checked; if (a.type[ia] == b.type[ib]) ++sameType; }
    }
    ok(checked > 400 && sameType == checked, "вещество в каждой клетке то же",
       std::to_string(sameType) + " из " + std::to_string(checked));

    // Главное: сохранение не привязано к номерам. Подменяем в файле имя
    // вещества на несуществующее — такие частицы обязаны пропасть, а
    // остальные загрузиться. Так же поведёт себя сохранение, сделанное
    // сборкой с другим набором веществ.
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::vector<char> raw;
        std::fseek(f, 0, SEEK_END); raw.resize(std::ftell(f)); std::fseek(f, 0, SEEK_SET);
        if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) return 1;
        std::fclose(f);
        // ищем имя WATER в палитре и портим его
        const char* needle = "WATER";
        for (size_t i = 0; i + 5 < raw.size(); ++i) {
            if (std::memcmp(&raw[i], needle, 5) == 0) { raw[i] = 'Q'; break; }
        }
        const std::string broken = "/tmp/проверка_чужое_вещество.upt";
        f = std::fopen(broken.c_str(), "wb");
        if (std::fwrite(raw.data(), 1, raw.size(), f) != raw.size()) return 1;
        std::fclose(f);

        World c(W, H);
        int lost = 0;
        const bool okLoad = loadWorld(c, broken, err, &lost);
        ok(okLoad, "сохранение с незнакомым веществом всё равно читается", err);
        int left = 0;
        for (int i = 0; i < c.maxUsed; ++i) if (c.alive[i] == 1) ++left;
        ok(lost > 0 && left == before - lost,
           "незнакомое вещество пропущено, остальное на месте",
           "пропущено " + std::to_string(lost) + ", осталось " + std::to_string(left));
        bool noWater = true;
        for (int i = 0; i < c.maxUsed; ++i)
            if (c.alive[i] == 1 && c.type[i] == WATER) noWater = false;
        ok(noWater, "воды в мире не осталось");
    }

    // Мусор вместо файла не роняет игру.
    {
        const std::string junk = "/tmp/проверка_мусор.upt";
        std::FILE* f = std::fopen(junk.c_str(), "wb");
        const char* s = "это не сохранение, а просто текст";
        if (std::fwrite(s, 1, std::strlen(s), f) != std::strlen(s)) return 1;
        std::fclose(f);
        World d(W, H);
        ok(!loadWorld(d, junk, err), "мусор отвергнут с объяснением", err);
        ok(!loadWorld(d, "/tmp/такого_файла_нет.upt", err), "нет файла — внятная ошибка", err);
    }

    std::printf("\nИТОГО: провалено %d\n", fails);
    return fails ? 1 : 0;
}
