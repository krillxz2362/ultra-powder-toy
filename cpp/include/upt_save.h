// Сохранение и загрузка мира.
//
// Номера веществ в сохранение НЕ пишутся. Их порядок меняется от
// версии к версии: стоит добавить вещество в середину списка — и все
// старые сохранения превратятся в кашу из случайных веществ. Поэтому в
// заголовке лежит палитра имён, а частицы ссылаются на неё. Вещество,
// которого в текущей сборке нет, просто пропускается, и игра говорит
// сколько.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace upt {

class World;

// Что лежит в сохранении — можно узнать, не загружая его.
struct SaveInfo {
    int      version = 0;
    int      width = 0, height = 0;
    int      particles = 0;
    int      substances = 0;     // сколько разных веществ в палитре
    uint64_t when = 0;           // когда записано, секунды эпохи
};

bool saveWorld(const World& w, const std::string& path, std::string& err);

// skipped — сколько частиц пропущено: их вещества нет в этой сборке.
bool loadWorld(World& w, const std::string& path, std::string& err,
               int* skipped = nullptr);

bool readSaveInfo(const std::string& path, SaveInfo& info, std::string& err);

// Папка сохранений. На телефоне это внутренняя папка приложения —
// её не видно в файловом менеджере и она чистится вместе с игрой.
std::string savesDir();

// Одно сохранение в папке: имя файла и что внутри.
struct SaveEntry {
    std::string name;        // имя файла без папки
    std::string path;
    SaveInfo    info;
    bool        readable = false;   // заголовок разобрался
    uint64_t    bytes = 0;
};

// Все сохранения папки, новые сверху. Нужен менеджеру в ветке UptA.
std::vector<SaveEntry> listSaves();
bool removeSave(const std::string& path);

}  // namespace upt
