#include "upt_save.h"

#ifdef UPT_WITH_SDL
#include <SDL.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <dirent.h>
#include <map>
#include <sys/stat.h>
#include <vector>

#include "upt_core.h"

namespace upt {

namespace {

constexpr char MAGIC[4] = {'U', 'P', 'T', 'S'};
constexpr uint16_t VERSION = 1;

// Пишем и читаем явно по байтам, младшим вперёд. Полагаться на
// раскладку структур в памяти нельзя: сохранение с телефона должно
// читаться на столе и наоборот.
struct Writer {
    std::vector<uint8_t> buf;
    void u8(uint8_t v)  { buf.push_back(v); }
    void u16(uint16_t v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16((v >> 16) & 0xFFFF); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v));
                           u32(static_cast<uint32_t>(v >> 32)); }
    void i32(int32_t v)  { u32(static_cast<uint32_t>(v)); }
    void f32(double v) {
        const float f = static_cast<float>(v);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        u32(bits);
    }
    void str(const char* s) {
        const size_t n = std::strlen(s);
        u8(static_cast<uint8_t>(n > 255 ? 255 : n));
        for (size_t i = 0; i < n && i < 255; ++i) u8(static_cast<uint8_t>(s[i]));
    }
};

struct Reader {
    const uint8_t* p = nullptr;
    size_t left = 0;
    bool bad = false;
    uint8_t u8() {
        if (left < 1) { bad = true; return 0; }
        --left; return *p++;
    }
    uint16_t u16() { uint16_t a = u8(); return static_cast<uint16_t>(a | (u8() << 8)); }
    uint32_t u32() { uint32_t a = u16(); return a | (static_cast<uint32_t>(u16()) << 16); }
    uint64_t u64() { uint64_t a = u32(); return a | (static_cast<uint64_t>(u32()) << 32); }
    int32_t  i32() { return static_cast<int32_t>(u32()); }
    double   f32() {
        const uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    std::string str() {
        const int n = u8();
        std::string s;
        for (int i = 0; i < n; ++i) s.push_back(static_cast<char>(u8()));
        return s;
    }
};

int idByKey(const std::string& key) {
    for (int i = 0; i < SUBSTANCE_COUNT; ++i)
        if (key == SUBSTANCES[i].key) return i;
    return -1;
}

bool readWhole(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "нет файла: " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); err = "пустой файл"; return false; }
    out.resize(static_cast<size_t>(n));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) { err = "файл прочитался не целиком"; return false; }
    return true;
}

}  // namespace

bool saveWorld(const World& w, const std::string& path, std::string& err) {
    // Палитра: собираем имена всех веществ, какие есть в мире. Сюда же
    // идут остатки горения (residue) — это тоже ссылка на вещество.
    std::map<int, uint16_t> toPal;
    std::vector<int> pal;
    auto put = [&](int id) -> uint16_t {
        if (id < 0 || id >= SUBSTANCE_COUNT) id = 0;
        auto it = toPal.find(id);
        if (it != toPal.end()) return it->second;
        const uint16_t idx = static_cast<uint16_t>(pal.size());
        toPal[id] = idx;
        pal.push_back(id);
        return idx;
    };

    int count = 0;
    for (int i = 0; i < w.maxUsed; ++i) {
        if (w.alive[i] != 1) continue;
        ++count;
        put(w.type[i]);
        put(w.residue[i]);
    }

    Writer o;
    for (char c : MAGIC) o.u8(static_cast<uint8_t>(c));
    o.u16(VERSION);
    o.u16(0);                                   // место под признаки
    o.i32(w.width());
    o.i32(w.height());
    o.i32(count);
    o.u64(static_cast<uint64_t>(std::time(nullptr)));
    o.u16(static_cast<uint16_t>(pal.size()));
    for (int id : pal) o.str(SUBSTANCES[id].key);

    for (int i = 0; i < w.maxUsed; ++i) {
        if (w.alive[i] != 1) continue;
        o.u16(toPal[w.type[i]]);
        o.u16(toPal[w.residue[i]]);
        o.f32(w.px[i]);   o.f32(w.py[i]);
        o.f32(w.vx[i]);   o.f32(w.vy[i]);
        o.f32(w.tmp[i]);
        o.f32(w.lat[i]);
        o.f32(w.burnT[i]);
        o.i32(w.life[i]);
        o.i32(w.burn[i]);
        o.u8(w.shd[i]);
        o.u8(w.dir[i]);
        o.u8(w.settled[i]);
    }

    // Пишем во временный файл и переименовываем: если игру закроют на
    // середине записи, прежнее сохранение останется целым.
    const std::string tmpPath = path + ".tmp";
    std::FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) { err = "не открыть для записи: " + tmpPath; return false; }
    const size_t wrote = std::fwrite(o.buf.data(), 1, o.buf.size(), f);
    std::fclose(f);
    if (wrote != o.buf.size()) { err = "записалось не всё"; std::remove(tmpPath.c_str()); return false; }
    std::remove(path.c_str());
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        err = "не переименовать " + tmpPath;
        return false;
    }
    return true;
}

bool readSaveInfo(const std::string& path, SaveInfo& info, std::string& err) {
    std::vector<uint8_t> data;
    if (!readWhole(path, data, err)) return false;
    Reader r{data.data(), data.size(), false};
    for (int i = 0; i < 4; ++i)
        if (r.u8() != static_cast<uint8_t>(MAGIC[i])) { err = "это не сохранение"; return false; }
    info.version = r.u16();
    r.u16();
    info.width = r.i32();
    info.height = r.i32();
    info.particles = r.i32();
    info.when = r.u64();
    info.substances = r.u16();
    if (r.bad) { err = "заголовок оборван"; return false; }
    return true;
}

bool loadWorld(World& w, const std::string& path, std::string& err, int* skipped) {
    std::vector<uint8_t> data;
    if (!readWhole(path, data, err)) return false;
    Reader r{data.data(), data.size(), false};

    for (int i = 0; i < 4; ++i)
        if (r.u8() != static_cast<uint8_t>(MAGIC[i])) { err = "это не сохранение"; return false; }
    const int version = r.u16();
    if (version > VERSION) {
        err = "сохранение из новой версии игры (" + std::to_string(version) + ")";
        return false;
    }
    r.u16();
    const int sw = r.i32();
    const int sh = r.i32();
    const int count = r.i32();
    r.u64();
    const int palN = r.u16();
    if (r.bad || sw <= 0 || sh <= 0 || count < 0) { err = "заголовок испорчен"; return false; }

    // Имя -> номер в этой сборке. Чего нет — помечаем как -1.
    std::vector<int> pal(static_cast<size_t>(palN), -1);
    for (int i = 0; i < palN; ++i) {
        const std::string key = r.str();
        pal[i] = idByKey(key);
    }
    if (r.bad) { err = "палитра имён оборвана"; return false; }

    w.clearWorld();
    int lost = 0;
    for (int n = 0; n < count; ++n) {
        const int ti = r.u16();
        const int ri = r.u16();
        const double px = r.f32(), py = r.f32();
        const double vx = r.f32(), vy = r.f32();
        const double tp = r.f32(), lt = r.f32(), bt = r.f32();
        const int32_t life = r.i32(), burn = r.i32();
        const uint8_t shd = r.u8(), dir = r.u8(), set = r.u8();
        if (r.bad) { err = "файл оборван на частице " + std::to_string(n); return false; }

        if (ti < 0 || ti >= palN || pal[ti] < 0) { ++lost; continue; }
        const int x = static_cast<int>(px), y = static_cast<int>(py);
        if (x < 0 || y < 0 || x >= w.width() || y >= w.height()) { ++lost; continue; }

        const int i = w.create(x, y, pal[ti], shd);
        if (i < 0) { ++lost; continue; }
        w.px[i] = px;  w.py[i] = py;
        w.vx[i] = vx;  w.vy[i] = vy;
        w.tmp[i] = tp; w.lat[i] = lt; w.burnT[i] = bt;
        w.life[i] = life; w.burn[i] = burn;
        w.dir[i] = dir; w.settled[i] = set;
        w.residue[i] = static_cast<uint16_t>(
            (ri >= 0 && ri < palN && pal[ri] >= 0) ? pal[ri] : 0);
        w.wakeCell(x, y);
    }
    if (skipped) *skipped = lost;
    (void)sw; (void)sh;     // мир может быть другого размера — что влезло, то влезло
    return true;
}

std::vector<SaveEntry> listSaves() {
    std::vector<SaveEntry> out;
    const std::string dir = savesDir();
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".upt") != 0) continue;
        SaveEntry s;
        s.name = name;
        s.path = dir + name;
        struct stat st {};
        if (stat(s.path.c_str(), &st) == 0) s.bytes = static_cast<uint64_t>(st.st_size);
        std::string err;
        s.readable = readSaveInfo(s.path, s.info, err);
        out.push_back(s);
    }
    closedir(d);
    // Новые сверху: так удобнее, последнее сохранение всегда первое.
    std::sort(out.begin(), out.end(), [](const SaveEntry& a, const SaveEntry& b) {
        if (a.info.when != b.info.when) return a.info.when > b.info.when;
        return a.name < b.name;
    });
    return out;
}

bool removeSave(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

std::string savesDir() {
    // SDL знает, куда класть: на телефоне это внутренняя папка
    // приложения (скрытая от файлового менеджера), на столе — обычная
    // папка настроек пользователя.
#ifdef UPT_WITH_SDL
    char* p = SDL_GetPrefPath("krillxz2362", "UltraPowderToy");
    if (p) { std::string s(p); SDL_free(p); return s; }
#endif
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
    return base + "/.ultra-powder-toy/";
}

}  // namespace upt
