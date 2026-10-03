-- export_cpp.lua — выгружает вещества и химию из работающего ядра в
-- заголовки C++. Руками 310 веществ и 1152 пары не переписывают: любая
-- опечатка здесь была бы расхождением с эталоном, которое мы потом
-- неделю ищем в физике.
--
-- Запуск: python3 run.py tools/export_cpp.lua

local E  = require("data.elements")
local RX = require("data.chemrx")

local OUT = "/home/user/sandbox/cpp/include/"

local function esc(s)
    return (tostring(s):gsub("\\", "\\\\"):gsub('"', '\\"'))
end

local function num(v)
    if v == nil then return "0" end
    if v ~= v then return "0" end                       -- не-число
    if v == math.huge then return "1e30" end
    if v == -math.huge then return "-1e30" end
    if v == math.floor(v) and math.abs(v) < 1e15 then
        return string.format("%d", v)
    end
    return string.format("%.9g", v)
end

----------------------------------------------------------------------
-- 1. Вещества
----------------------------------------------------------------------
local f = assert(io.open(OUT .. "upt_substances.h", "w"))
f:write([[
// upt_substances.h — СОЗДАЁТСЯ АВТОМАТИЧЕСКИ, ПРАВИТЬ БЕССМЫСЛЕННО.
// Выгружено из game/data/*.lua скриптом tools/export_cpp.lua.
//
// Все величины справочные: плотность в кг/м3, теплоёмкость в кДж/(кг·К),
// температуры в градусах Цельсия.
#pragma once
#include <cstdint>

namespace upt {

enum State : uint8_t { EMPTY = 0, POWDER = 1, LIQUID = 2, GAS = 3, SOLID = 4 };

struct Substance {
    const char* key;        // ключ движка, он же имя в сохранении
    const char* name;       // русское название
    const char* formula;    // формула или символ, пусто у исходных веществ
    int16_t     z;          // номер в таблице Менделеева, 0 если не элемент
    uint8_t     state;
    int32_t     density;    // кг/м3
    double      temp0;      // температура появления
    double      cond, cap, expans, tension, visc, latF, latV;
    double      grav, drag, elast, jit, repose, advec, airDrag, airLoss;
    double      hotAir, expand;
    uint8_t     fixed, acidProof, active;
    double      meltAt;  int32_t meltTo;
    double      boilAt;  int32_t boilTo;
    double      coolAt;  int32_t coolTo;
    double      igniteAt, flam;
    int32_t     residue;
    int32_t     life, decayTo;
    double      blast, shock, burnTemp;
    int32_t     burnLife, burnTo;
    double      oxyUse, oxyNeed;
    uint8_t     r, g, b, shade;
};

]])

f:write("inline constexpr int SUBSTANCE_COUNT = ", E.count, ";\n\n")
f:write("inline constexpr Substance SUBSTANCES[SUBSTANCE_COUNT] = {\n")

for id = 0, E.count - 1 do
    local e = E.byId[id]
    local c = e.color or { 128, 128, 128 }
    f:write(string.format(
        '    { "%s", "%s", "%s", %d, %d, %d, %s,\n' ..
        '      %s, %s, %s, %s, %s, %s, %s,\n' ..
        '      %s, %s, %s, %s, %s, %s, %s, %s,\n' ..
        '      %s, %s, %d, %d, %d,\n' ..
        '      %s, %d, %s, %d, %s, %d,\n' ..
        '      %s, %s, %d, %d, %d,\n' ..
        '      %s, %s, %s, %d, %d, %s, %s,\n' ..
        '      %d, %d, %d, %d },  // %d %s\n',
        esc(e.key), esc(e.name), esc(e.sym or e.formula or ""), e.z or 0,
        E.stateOf[id], E.densityOf[id], num(E.tempOf[id]),
        num(E.condOf[id]), num(E.capArr[id]), num(E.expansArr[id]),
        num(E.tensionArr[id]), num(E.viscArr[id]), num(E.latFArr[id]), num(E.latVArr[id]),
        num(E.gravArr[id]), num(E.dragArr[id]), num(E.elastArr[id]), num(E.jitArr[id]),
        num(E.reposeArr[id]), num(E.advecArr[id]), num(E.airDragArr[id]), num(E.airLossArr[id]),
        num(E.hotAirArr[id]), num(E.expandArr[id]),
        E.fixedOf[id], E.acidProofOf[id], E.activeArr[id],
        num(E.meltAt[id]), E.meltTo[id], num(E.boilAt[id]), E.boilTo[id],
        num(E.coolAt[id]), E.coolTo[id],
        num(E.igniteAt[id]), num(E.flamOf[id]), E.residueOf[id],
        E.lifeOf[id], E.decayToOf[id],
        num(E.blastOf[id]), num(E.shockOf[id]), num(E.burnTempOf[id]),
        E.burnLifeOf[id], E.burnToOf[id], num(E.oxyUseOf[id]), num(E.oxyNeedOf[id]),
        c[1], c[2], c[3], e.shade or 0, id, esc(e.name)))
end
f:write("};\n\n} // namespace upt\n")
f:close()

----------------------------------------------------------------------
-- 2. Химия
----------------------------------------------------------------------
-- Плоский массив N×N в исходник не выгружаем: 96 100 чисел раздуют
-- сборку впустую. Выгружаем разреженный список пар, массив строится
-- при запуске за доли миллисекунды.
local g = assert(io.open(OUT .. "upt_chem.h", "w"))
g:write([[
// upt_chem.h — СОЗДАЁТСЯ АВТОМАТИЧЕСКИ, ПРАВИТЬ БЕССМЫСЛЕННО.
// Выгружено скриптом tools/export_cpp.lua.
#pragma once
#include <cstdint>

namespace upt {

// Одна реакция: что получится и при каких условиях.
struct Reaction {
    int32_t o1, o2;     // во что превращаются; -1 — частица исчезает
    double  tmin, tmax; // окно температур
    double  prob;       // вероятность за шаг
    double  heat;       // тепловой эффект, градусов
    int32_t catFirst;   // смещение в CAT_IDS, -1 если катализатор не нужен
    int32_t catCount;
};

// Пара веществ и номер реакции для неё.
struct PairRef { int32_t a, b, rx; };

// Разложение при нагреве.
struct Decay { int32_t from, o1, o2; double tmin, prob; };

]])

local catIds, catOf = {}, {}
for i, r in ipairs(RX.pairList) do
    if r.cat then
        local first = #catIds
        local n = 0
        for id in pairs(r.cat) do catIds[#catIds + 1] = id; n = n + 1 end
        catOf[i] = { first, n }
    end
end

g:write("inline constexpr int CAT_ID_COUNT = ", math.max(#catIds, 1), ";\n")
g:write("inline constexpr int32_t CAT_IDS[CAT_ID_COUNT] = {")
if #catIds == 0 then g:write("0") else
    for i, v in ipairs(catIds) do
        if (i - 1) % 16 == 0 then g:write("\n    ") end
        g:write(v, ", ")
    end
end
g:write("\n};\n\n")

g:write("inline constexpr int REACTION_COUNT = ", #RX.pairList, ";\n")
g:write("inline constexpr Reaction REACTIONS[REACTION_COUNT] = {\n")
for i, r in ipairs(RX.pairList) do
    local cf, cn = -1, 0
    if catOf[i] then cf, cn = catOf[i][1], catOf[i][2] end
    g:write(string.format("    { %d, %d, %s, %s, %s, %s, %d, %d },\n",
        r.o1, r.o2, num(r.t), num(r.tmax), num(r.p), num(r.heat), cf, cn))
end
g:write("};\n\n")

-- разреженный список пар
local pairs_ = {}
local N = RX.N
for a = 0, N - 1 do
    local row = a * N
    for b = 0, N - 1 do
        local v = RX.pairIdx[row + b]
        if v ~= 0 then pairs_[#pairs_ + 1] = { a, b, v } end
    end
end
g:write("inline constexpr int PAIR_COUNT = ", #pairs_, ";\n")
g:write("inline constexpr PairRef PAIRS[PAIR_COUNT] = {\n")
for _, p in ipairs(pairs_) do
    g:write(string.format("    { %d, %d, %d },\n", p[1], p[2], p[3]))
end
g:write("};\n\n")

local decs = {}
for id = 0, N - 1 do
    local di = RX.decIdx[id]
    if di ~= 0 then
        local d = RX.decList[di]
        decs[#decs + 1] = string.format("    { %d, %d, %d, %s, %s },\n",
            id, d.o1, d.o2 or -1, num(d.t), num(d.p))
    end
end
g:write("inline constexpr int DECAY_COUNT = ", #decs, ";\n")
g:write("inline constexpr Decay DECAYS[DECAY_COUNT] = {\n")
for _, s in ipairs(decs) do g:write(s) end
g:write("};\n\n")

-- Пометка «вещество участвует в химии». Выгружается, а не выводится
-- заново на стороне C++: правило, по которому она ставится, живёт в
-- chemrx.lua, и повторять его во втором месте — заводить вторую правду.
g:write("inline constexpr uint8_t RX_ACTIVE[", E.count, "] = {")
for id = 0, E.count - 1 do
    if id % 32 == 0 then g:write("\n    ") end
    g:write(RX.actArr[id], ", ")
end
g:write("\n};\n\n")

-- Таблица Менделеева для экрана ХИМИЯ: место в сетке, символ, имя,
-- цвет CPK. Интерфейсу на C++ нужны те же данные, что и экрану LÖVE,
-- и браться они обязаны из одного места.
local chem = require("data.chem")
local idByZ = {}
for i = 1, E.count do
    local e = E.list[i]
    if e and e.z and e.z > 0 and not idByZ[e.z] then idByZ[e.z] = e.id end
end
g:write("struct ChemCell { int16_t z, gx, gy; int32_t id;\n")
g:write("                  const char* sym; const char* name; uint8_t r, g, b; };\n\n")
g:write("inline constexpr int CHEM_CELL_COUNT = ", #chem.el, ";\n")
g:write("inline constexpr ChemCell CHEM_CELLS[CHEM_CELL_COUNT] = {\n")
for _, e in ipairs(chem.el) do
    g:write(string.format("    { %d, %d, %d, %d, \"%s\", \"%s\", %d, %d, %d },\n",
        e.z, e.x, e.y, idByZ[e.z] or 0, e.sym, e.ru, e.r, e.g, e.b))
end
g:write("};\n\n} // namespace upt\n")
g:close()

print(string.format("выгружено: веществ %d, реакций %d, пар %d, разложений %d, катализаторов %d",
    E.count, #RX.pairList, #pairs_, #decs, #catIds))
