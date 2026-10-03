-- chemrx.lua — превращения веществ друг в друга.
--
-- Устройство. Вся химия сводится к одной таблице пар, которая строится
-- ОДИН РАЗ при запуске. В кадре на частицу приходится одно обращение по
-- числовому ключу, без перебора правил и без строковых сравнений: при
-- 25 тысячах частиц перебор правил в цикле съедал бы весь кадр.
--
-- Таблица собирается из двух источников:
--   1. ПРАВИЛО. Для пар «металл + неметалл» формула продукта выводится
--      по валентностям (группа таблицы Менделеева), и если такое
--      соединение у нас есть — реакция появляется сама.
--   2. СПИСОК. Явные реакции из compounds.lua с настоящими условиями,
--      катализаторами и уравнениями. Список всегда главнее правила.

local E    = require("data.elements")
local chem = require("data.chem")
local Cp   = require("data.compounds")

local R = {}
local N = E.count
R.N = N

----------------------------------------------------------------------
-- Разбор названий веществ
----------------------------------------------------------------------
-- В реакциях вещества зовутся по-химически: "O2", "H2O", "Fe". Движок
-- же знает ключи: кислород это O / O_L / O_G, вода — ВОДА, ПАР и ЛЁД.
-- Здесь одно переводится в другое.

-- двухатомные газы и вещества, которые в движке были с самого начала
local ALIAS = {
    H2 = "H", O2 = "O", N2 = "N", F2 = "F",
    Cl2 = "CL", Br2 = "BR", I2 = "I",
    H2O = "WATER", SiO2 = "SAND", NaCl = "SALT",
}
-- дополнительные формы: лёд и пар — тоже вода, расплав металла — тот же металл
local EXTRA = { WATER = { "STEAM", "ICE" } }

local byF = {}
for _, c in ipairs(Cp.list) do byF[c.f] = c end
R.byF = byF

-- все формы вещества (твёрдая, жидкая, газовая) — реагируют любые
local function formsOf(name)
    local c = byF[name]
    local base = c and c.key or ALIAS[name] or name:upper()
    local ids, seen = {}, {}
    local function add(key)
        local id = E.byKey[key]
        if id and not seen[id] then seen[id] = true; ids[#ids + 1] = id end
    end
    add(base); add(base .. "_L"); add(base .. "_G")
    for _, k in ipairs(EXTRA[base] or {}) do add(k) end
    return ids
end

-- во что превращать: форма, которая существует при комнатной температуре
local function productOf(name)
    local c = byF[name]
    local base = c and c.key or ALIAS[name] or name:upper()
    local best
    for _, suf in ipairs({ "", "_L", "_G" }) do
        local id = E.byKey[base .. suf]
        if id then
            local e = E.byId[id]
            best = best or id
            if (e.temp or 22) == E.ROOM_TEMP then return id end
        end
    end
    return best
end
R.formsOf, R.productOf = formsOf, productOf

----------------------------------------------------------------------
-- 1. ПРАВИЛО: металл + неметалл по валентностям
----------------------------------------------------------------------
-- Валентность по номеру группы. Для главных подгрупп это школьное
-- правило, для переходных металлов берём самую ходовую степень.
local MAIN_VAL = { [1]=1, [2]=2, [13]=3, [14]=4, [15]=3, [16]=2, [17]=1 }
local MET = { ["щелоч"]=true, ["щзем"]=true, ["перех"]=true, ["постпер"]=true }
local NONMET = { ["немет"]=true, ["галоген"]=true }

-- условия по категории металла: с какой температуры, как охотно, сколько тепла
local COND = {
    ["щелоч"]   = { t = -50, p = 0.70, heat = 200 },
    ["щзем"]    = { t =  50, p = 0.50, heat = 180 },
    ["перех"]   = { t = 200, p = 0.35, heat = 110 },
    ["постпер"] = { t = 250, p = 0.30, heat =  90 },
}

local function gcd(a, b) while b ~= 0 do a, b = b, a % b end return a end

local function part(sym, n)
    if n == 1 then return sym end
    return sym .. n
end

-- Формула по валентностям: Mg(II) и Cl(I) дают MgCl2.
local function crossFormula(ms, vm, xs, vx)
    local g = gcd(vm, vx)
    local a, b = vx / g, vm / g
    return part(ms, a) .. part(xs, b)
end

local function valenceOf(e)
    if e.cat == "перех" then
        return (e.sym == "Fe" or e.sym == "Al" or e.sym == "Cr") and 3 or 2
    end
    return MAIN_VAL[e.x]
end

-- Неметаллы, с которыми правило вообще пробует соединять: галогены,
-- кислород и сера. Азот с углеродом требуют условий, их ведёт список.
local RULE_X = { O = 2, F = 1, Cl = 1, Br = 1, I = 1, S = 2 }

local generated = 0
local ruleRx = {}
for _, m in ipairs(chem.el) do
    if MET[m.cat] then
        local vm = valenceOf(m)
        local cd = COND[m.cat]
        if vm and cd then
            for xs, vx in pairs(RULE_X) do
                local f = crossFormula(m.sym, vm, xs, vx)
                if byF[f] or f == "NaCl" or f == "SiO2" then
                    local a = (xs == "O" or xs == "S" or xs == "F") and (xs .. "2") or (xs .. "2")
                    if xs == "S" then a = "S" end
                    ruleRx[#ruleRx + 1] = {
                        a = a, b = m.sym, out = f, out2 = nil,
                        t = cd.t, tmax = 9999, p = cd.p, heat = cd.heat,
                        eq = m.ru .. " + " .. xs .. " → " .. f,
                        rule = true,
                    }
                    generated = generated + 1
                end
            end
        end
    end
end
R.generated = generated

----------------------------------------------------------------------
-- 2. Сборка таблицы пар
----------------------------------------------------------------------
-- Таблица пар: плоский массив индексов N×N вместо хеш-таблицы Lua.
-- При 310 веществах это 96 100 чисел по 4 байта, меньше 400 КБ, зато
-- обращение становится чтением по смещению, а не поиском по хешу —
-- и такой массив переносится в C++ один в один.
local ffi = require("ffi")
R.pairIdx  = ffi.new("int32_t[?]", N * N)
R.pairList = {}
R.act  = {}
R.recipes = {}          -- [id продукта] = список способов получить
R.all = {}              -- все реакции подряд, для справочника

local KILL = -1

local function addRecipe(outName, rec)
    local id = productOf(outName)
    if not id then return end
    local t = R.recipes[id]
    if not t then t = {}; R.recipes[id] = t end
    t[#t + 1] = rec
end

local function addPair(r)
    local A, B = formsOf(r.a), formsOf(r.b)
    if #A == 0 or #B == 0 then return false end
    local o1 = r.out and productOf(r.out) or KILL
    local o2 = r.out2 and productOf(r.out2) or KILL
    if not o1 then return false end

    local cat
    if r.cat then
        cat = {}
        for _, id in ipairs(formsOf(r.cat)) do cat[id] = true end
        if not next(cat) then cat = nil end
    end

    local fwd = { o1 = o1, o2 = o2, t = r.t, tmax = r.tmax or 9999,
                  p = r.p, heat = r.heat or 0, cat = cat, eq = r.eq, rule = r.rule }
    local bwd = { o1 = o2, o2 = o1, t = fwd.t, tmax = fwd.tmax,
                  p = fwd.p, heat = fwd.heat, cat = cat, eq = r.eq, rule = r.rule }

    local list = R.pairList
    list[#list + 1] = fwd
    local nf = #list
    list[#list + 1] = bwd
    local nb = #list

    local idx = R.pairIdx
    for _, ia in ipairs(A) do
        for _, ib in ipairs(B) do
            if ia ~= ib then
                -- 0 в массиве означает «реакции нет»; номера записи
                -- хранятся как есть, поэтому список начинается с 1.
                if idx[ia * N + ib] == 0 then idx[ia * N + ib] = nf end
                if idx[ib * N + ia] == 0 then idx[ib * N + ia] = nb end
                R.act[ia] = true
                R.act[ib] = true
            end
        end
    end

    R.all[#R.all + 1] = r
    if r.out  then addRecipe(r.out,  r) end
    if r.out2 then addRecipe(r.out2, r) end
    return true
end

-- Сначала список: он должен побеждать правило.
local added = 0
for _, r in ipairs(Cp.rx) do if addPair(r) then added = added + 1 end end
local ruleAdded = 0
for _, r in ipairs(ruleRx) do if addPair(r) then ruleAdded = ruleAdded + 1 end end
R.listCount, R.ruleCount = added, ruleAdded

----------------------------------------------------------------------
-- 3. Разложение при нагреве
----------------------------------------------------------------------
R.dec = {}
R.decIdx  = ffi.new("int32_t[?]", N)
R.decList = {}
for _, d in ipairs(Cp.dec) do
    local o1 = productOf(d.out)
    local o2 = d.out2 and productOf(d.out2) or nil
    if o1 then
        local rec = { o1 = o1, o2 = o2, t = d.t, p = d.p, eq = d.eq }
        R.decList[#R.decList + 1] = rec
        local n = #R.decList
        for _, id in ipairs(formsOf(d.a)) do
            R.dec[id] = rec
            if R.decIdx[id] == 0 then R.decIdx[id] = n end
            R.act[id] = true
        end
        addRecipe(d.out, { a = d.a, out = d.out, out2 = d.out2, t = d.t,
                           p = d.p, eq = d.eq, dec = true })
        if d.out2 then
            addRecipe(d.out2, { a = d.a, out = d.out, out2 = d.out2, t = d.t,
                                p = d.p, eq = d.eq, dec = true })
        end
    end
end

-- Плоский массив «участвует ли вещество в химии вообще»: проверка в
-- горячем цикле должна быть одним обращением по индексу.
R.actArr = ffi.new("uint8_t[?]", N)
for id in pairs(R.act) do R.actArr[id] = 1 end

return R
