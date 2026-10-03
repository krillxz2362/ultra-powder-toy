-- Проверка физики. Без телефона и без LOVE.
package.path = "/home/user/sandbox/game/?.lua;" .. package.path

local E = require("data.elements")
local Sim = require("fiz.sim")
local abs = math.abs

math.randomseed(1234)
local K = E.byKey
local pass, fail = 0, 0

local function check(label, ok, detail)
    if ok then pass = pass + 1 else fail = fail + 1 end
    print(string.format("%s %s%s", ok and "[ОК]  " or "[ПЛОХО]", label,
        detail and ("  — " .. detail) or ""))
end

local function count(s, id)
    local c = 0
    for i = 0, s.maxUsed - 1 do
        if s.alive[i] == 1 and s.ptype[i] == id then c = c + 1 end
    end
    return c
end

local function avgRow(s, id)
    local sum, n = 0, 0
    for i = 0, s.maxUsed - 1 do
        if s.alive[i] == 1 and s.ptype[i] == id then sum = sum + s.py[i]; n = n + 1 end
    end
    if n == 0 then return nil end
    return sum / n
end

local function fill(s, x0, y0, x1, y1, id)
    for y = y0, y1 do for x = x0, x1 do s:create(x, y, id, math.random(0, 255)) end end
end

local function run(s, n) for i = 1, n do s:step() end end

-- Поставить вещество поверх существующего: движок намеренно не
-- перезаписывает занятую клетку, поэтому сначала освобождаем её.
local function place(s, x0, y0, x1, y1, id)
    for y = y0, y1 do for x = x0, x1 do
        s:killAt(x, y)
        s:create(x, y, id, math.random(0, 255))
    end end
end

local function integrity(s)
    local seen, bad, orph = {}, 0, 0
    for i = 0, s.maxUsed - 1 do
        if s.alive[i] == 1 then
            local x, y = math.floor(s.px[i]), math.floor(s.py[i])
            if x < 0 or y < 0 or x >= s.w or y >= s.h then bad = bad + 1
            else
                local ci = y * s.w + x
                if s.pmap[ci] ~= i + 1 then bad = bad + 1 end
                if seen[ci] then bad = bad + 1 end
                seen[ci] = true
            end
        end
    end
    for ci = 0, s.n - 1 do
        local occ = s.pmap[ci]
        if occ ~= 0 then
            local i = occ - 1
            if s.alive[i] ~= 1 then orph = orph + 1
            elseif math.floor(s.py[i]) * s.w + math.floor(s.px[i]) ~= ci then orph = orph + 1 end
        end
    end
    return bad, orph
end

----------------------------------------------------------------------
print("=== ЦЕЛОСТНОСТЬ ===")
local s0 = Sim.new(120, 160)
fill(s0, 20, 10, 100, 50, K.SAND)
fill(s0, 20, 60, 100, 80, K.WATER)
fill(s0, 40, 130, 80, 140, K.STONE)
run(s0, 400)
local bad, orph = integrity(s0)
check("частицы и карта занятости согласованы", bad == 0 and orph == 0,
    string.format("рассогласований %d, осиротевших %d", bad, orph))

----------------------------------------------------------------------
print()
print("=== ЗАКОН АРХИМЕДА (на настоящих плотностях) ===")

-- масло 900 легче воды 1000 — всплывает
local s1 = Sim.new(60, 120)
fill(s1, 0, 50, 59, 99, K.WATER)
fill(s1, 20, 100, 40, 115, K.OIL)
run(s1, 1500)
local aw, ao = avgRow(s1, K.WATER), avgRow(s1, K.OIL)
check("масло (900) всплывает над водой (1000)", ao and aw and ao < aw,
    string.format("масло %.1f, вода %.1f", ao or -1, aw or -1))

-- гравий 1800 тонет в воде
local s2 = Sim.new(60, 120)
fill(s2, 0, 40, 59, 110, K.WATER)
fill(s2, 25, 10, 35, 20, K.GRAVEL)
run(s2, 900)
local ag, aw2 = avgRow(s2, K.GRAVEL), avgRow(s2, K.WATER)
check("гравий (1800) тонет в воде", ag and aw2 and ag > aw2,
    string.format("гравий %.1f, вода %.1f", ag or -1, aw2 or -1))

-- песок 1600 ПЛАВАЕТ на ртути 13600 — так в жизни и есть
local s3 = Sim.new(60, 120)
fill(s3, 0, 60, 59, 110, K.MERCURY)
fill(s3, 20, 112, 40, 118, K.SAND)
run(s3, 1200)
local asd, am = avgRow(s3, K.SAND), avgRow(s3, K.MERCURY)
check("песок (1600) всплывает на ртути (13600)", asd and am and asd < am,
    string.format("песок %.1f, ртуть %.1f", asd or -1, am or -1))

-- пузырь газа идёт вверх сквозь воду
local s4 = Sim.new(60, 120)
fill(s4, 0, 20, 59, 110, K.WATER)
local gasRows = {}
for y = 100, 106 do for x = 28, 32 do
    s4:killAt(x, y); s4:create(x, y, K.GAS, 128)
end end
local gStart = avgRow(s4, K.GAS)
run(s4, 400)
local gEnd = avgRow(s4, K.GAS)
check("пузырь газа всплывает сквозь воду", gEnd and gStart and gEnd < gStart - 20,
    string.format("был на %.0f, стал на %.0f", gStart or -1, gEnd or -1))

----------------------------------------------------------------------
print()
print("=== ТЕПЛОЁМКОСТЬ И СКРЫТАЯ ТЕПЛОТА ===")

-- Теплоёмкость проверяем точно: одна частица касается нагревателя,
-- смотрим прирост за первый шаг и сверяем с расчётом
--   dT = (1800 - 22) * min(cond, 1) * FLOW / max(cap, 1)
local function firstStepRise(mat, cond, cap)
    local s = Sim.new(12, 12)
    s:create(5, 5, mat, 128)
    s:create(5, 6, K.HEAT, 128)
    local t0 = s.ptmp[s.pmap[5 * s.w + 5] - 1]
    s:heat()
    local t1 = s.ptmp[s.pmap[5 * s.w + 5] - 1]
    local predicted = (1800 - 22) * math.min(cond, 1.0) * 0.12 / math.max(cap, 1.0)
    return t1 - t0, predicted
end
local rW, pW = firstStepRise(K.WATER, 0.12, 4.18)
local rM, pM = firstStepRise(K.METAL, 0.95, 0.45)
check("прирост температуры воды совпадает с расчётом",
    math.abs(rW - pW) < pW * 0.25,
    string.format("намерено %.1f°, посчитано %.1f° (теплоёмкость 4.18)", rW, pW))
check("металл нагревается много быстрее воды", rM > rW * 5,
    string.format("металл %.0f° за шаг, вода %.1f° за шаг", rM, rW))

-- металл проводит тепло лучше дерева
local function conductTest(mat)
    local s = Sim.new(40, 60)
    for x = 0, 39 do s:create(x, 30, mat, 128) end
    s:create(0, 29, K.HEAT, 128)
    run(s, 900)
    local occ = s.pmap[30 * s.w + 10]
    return occ ~= 0 and s.ptmp[occ - 1] or -999
end
local tm, tw = conductTest(K.METAL), conductTest(K.WOOD)
check("металл проводит тепло лучше дерева", tm > tw + 20,
    string.format("в 10 клетках: металл %.0f°, дерево %.0f°", tm, tw))

-- скрытая теплота: вода стоит на 100° всё время кипения
local s5 = Sim.new(30, 40)
fill(s5, 5, 10, 24, 24, K.WATER)
for x = 5, 24 do s5:create(x, 25, K.HEAT, 128) end
-- Смотрим самую горячую каплю: средняя температура по всей воде
-- включает дальние холодные слои и полку кипения не покажет.
local plateau, hottest = 0, -999
for i = 1, 600 do
    s5:step()
    local mx = -999
    for k = 0, s5.maxUsed - 1 do
        if s5.alive[k] == 1 and s5.ptype[k] == K.WATER and s5.ptmp[k] > mx then
            mx = s5.ptmp[k]
        end
    end
    if mx > hottest then hottest = mx end
    if mx >= 98 and mx <= 103 then plateau = plateau + 1 end
end
check("вода задерживается на 100° (скрытая теплота)", plateau > 50,
    string.format("шагов на полке кипения %d", plateau))
check("вода не перегревается выше точки кипения", hottest < 106,
    string.format("самая горячая капля за всё время %.1f°", hottest))
check("кипение всё же доходит до пара", count(s5, K.STEAM) > 0,
    string.format("пара %d", count(s5, K.STEAM)))

----------------------------------------------------------------------
print()
print("=== УГОЛ ЕСТЕСТВЕННОГО ОТКОСА ===")

-- Меряем не ширину, а уклон кучи: отношение высоты к половине основания.
-- Это тангенс угла откоса, его можно сверить со справочником.
local function pileSlope(mat)
    local s = Sim.new(160, 100)
    for i = 1, 1600 do
        s:create(80, 2, mat, math.random(0, 255))
        s:step()
    end
    run(s, 500)
    local mn, mx, top = 1e9, -1, 1e9
    for k = 0, s.maxUsed - 1 do
        if s.alive[k] == 1 then
            local x, y = s.px[k], s.py[k]
            if x < mn then mn = x end
            if x > mx then mx = x end
            if y < top then top = y end
        end
    end
    return (99 - top) / ((mx - mn) * 0.5)
end
local slSand, slAsh, slGravel = pileSlope(K.SAND), pileSlope(K.ASH), pileSlope(K.GRAVEL)
local function deg(t) return math.deg(math.atan(t)) end
check("гравий лежит круче золы, зола круче песка",
    slGravel > slAsh and slAsh > slSand,
    string.format("песок %.0f°, зола %.0f°, гравий %.0f°",
        deg(slSand), deg(slAsh), deg(slGravel)))
check("угол откоса песка близок к настоящим 34°",
    deg(slSand) > 30 and deg(slSand) < 40,
    string.format("получилось %.0f°", deg(slSand)))
check("угол откоса гравия близок к настоящим 45°",
    deg(slGravel) > 40 and deg(slGravel) < 50,
    string.format("получилось %.0f°", deg(slGravel)))

----------------------------------------------------------------------
print()
print("=== ДАВЛЕНИЕ И ВЗРЫВЫ ===")

local s6 = Sim.new(120, 100)
fill(s6, 40, 60, 80, 80, K.SAND)
place(s6, 55, 70, 65, 75, K.TNT)
run(s6, 20)
local pBefore = 0
for i = 0, s6.air.n - 1 do if s6.air.pv[i] > pBefore then pBefore = s6.air.pv[i] end end
-- поджигаем
local tntN = count(s6, K.TNT)
for x = 55, 65 do
    local occ = s6.pmap[70 * s6.w + x]
    if occ ~= 0 then s6.ptmp[occ - 1] = 700 end
end
local pMax = 0
local vMax = 0
for i = 1, 120 do
    s6:step()
    for k = 0, s6.air.n - 1 do
        local v = s6.air.pv[k]; if v > pMax then pMax = v end
    end
    for k = 0, s6.maxUsed - 1 do
        if s6.alive[k] == 1 then
            local sp = math.abs(s6.pvx[k]) + math.abs(s6.pvy[k])
            if sp > vMax then vMax = sp end
        end
    end
end
check("взрыв тротила поднимает давление", pMax > 10,
    string.format("тротила заложено %d, давление до %.2f, в пике %.1f",
        tntN, pBefore, pMax))
check("взрыв расшвыривает вещество", vMax > 3,
    string.format("наибольшая скорость осколка %.1f клеток за шаг", vMax))

-- нитроглицерин взрывается от удара
local s7 = Sim.new(60, 120)
fill(s7, 0, 110, 59, 115, K.STONE)
fill(s7, 25, 5, 35, 12, K.NITRO)
local nitroBefore = count(s7, K.NITRO)
run(s7, 200)
check("нитро взрывается от удара", count(s7, K.NITRO) < nitroBefore * 0.5,
    string.format("было %d, осталось %d", nitroBefore, count(s7, K.NITRO)))

-- огонь создаёт избыточное давление, дым от него поднимается
local s8 = Sim.new(60, 80)
fill(s8, 25, 60, 35, 70, K.FIRE)
run(s8, 40)
local pFire = 0
for i = 0, s8.air.n - 1 do if s8.air.pv[i] > pFire then pFire = s8.air.pv[i] end end
check("огонь создаёт избыточное давление", pFire > 0.001,
    string.format("давление над костром %.4f", pFire))

----------------------------------------------------------------------
print()
print("=== ГОРЕНИЕ, КИСЛОТА, ЛАВА ===")

local s9 = Sim.new(60, 90)
fill(s9, 10, 40, 50, 60, K.WOOD)
local woodBefore = count(s9, K.WOOD)
fill(s9, 28, 38, 32, 39, K.FIRE)
local maxFire = 0
for i = 1, 900 do
    s9:step()
    local f = count(s9, K.FIRE); if f > maxFire then maxFire = f end
end
check("огонь расходится по дереву", count(s9, K.WOOD) < woodBefore * 0.7,
    string.format("дерева было %d, стало %d, пламени до %d",
        woodBefore, count(s9, K.WOOD), maxFire))
check("после пожара остаётся зола", count(s9, K.ASH) > 0,
    string.format("золы %d", count(s9, K.ASH)))

local s10 = Sim.new(40, 60)
fill(s10, 15, 20, 25, 30, K.FIRE)
run(s10, 600)
check("огонь без топлива гаснет", count(s10, K.FIRE) == 0,
    string.format("осталось %d", count(s10, K.FIRE)))

local s11 = Sim.new(40, 60)
fill(s11, 10, 30, 30, 45, K.STONE)
local stoneBefore = count(s11, K.STONE)
fill(s11, 10, 28, 30, 29, K.FIRE)
run(s11, 500)
check("камень не горит", count(s11, K.STONE) == stoneBefore,
    string.format("было %d, стало %d", stoneBefore, count(s11, K.STONE)))

local s12 = Sim.new(60, 90)
fill(s12, 0, 60, 59, 85, K.WATER)
fill(s12, 20, 20, 40, 35, K.LAVA)
local maxSteam = 0
for i = 1, 700 do
    s12:step()
    local c = count(s12, K.STEAM); if c > maxSteam then maxSteam = c end
end
check("лава кипятит воду в пар", maxSteam > 0, string.format("пара до %d", maxSteam))
check("лава застывает в камень", count(s12, K.STONE) > 0,
    string.format("камня %d", count(s12, K.STONE)))

local s13 = Sim.new(40, 60)
fill(s13, 5, 30, 34, 40, K.WOOD)
fill(s13, 5, 25, 34, 29, K.ACID)
local wB = count(s13, K.WOOD)
run(s13, 500)
check("кислота разъедает дерево", count(s13, K.WOOD) < wB * 0.6,
    string.format("было %d, стало %d", wB, count(s13, K.WOOD)))

local s14 = Sim.new(40, 60)
fill(s14, 5, 30, 34, 40, K.GLASS)
fill(s14, 5, 25, 34, 29, K.ACID)
local gB = count(s14, K.GLASS)
run(s14, 500)
check("стекло кислоту держит", count(s14, K.GLASS) == gB,
    string.format("было %d, стало %d", gB, count(s14, K.GLASS)))

-- термит плавит металл
local s15 = Sim.new(50, 70)
fill(s15, 10, 45, 40, 55, K.METAL)
fill(s15, 15, 35, 35, 42, K.THERMITE)
for x = 15, 35 do
    local occ = s15.pmap[35 * s15.w + x]
    if occ ~= 0 then s15.ptmp[occ - 1] = 1000 end
end
local metalBefore = count(s15, K.METAL)
local hotMetal = -999
for i = 1, 700 do
    s15:step()
    for k = 0, s15.maxUsed - 1 do
        if s15.alive[k] == 1 and s15.ptype[k] == K.METAL and s15.ptmp[k] > hotMetal then
            hotMetal = s15.ptmp[k]
        end
    end
end
check("термит раскаляет металл до плавления", hotMetal > 1400,
    string.format("металл разогрелся до %.0f° при точке плавления 1500°", hotMetal))
check("термит даёт расплав, а не улетающее пламя", count(s15, K.LAVA) > 50,
    string.format("расплава %d клеток, металла было %d, стало %d",
        count(s15, K.LAVA), metalBefore, count(s15, K.METAL)))


----------------------------------------------------------------------
print()
print("=== КОНВЕКЦИЯ ===")

local function tank(fromBelow)
    math.randomseed(5)
    local s = Sim.new(40, 60)
    local plate = {}
    if fromBelow then
        for y = 20, 49 do for x = 0, 39 do s:create(x, y, K.WATER, 128) end end
        for x = 0, 39 do plate[#plate+1] = s:create(x, 50, K.METAL, 128) end
        for x = 0, 39 do s:create(x, 19, K.GLASS, 128) end
    else
        for y = 21, 50 do for x = 0, 39 do s:create(x, y, K.WATER, 128) end end
        for x = 0, 39 do plate[#plate+1] = s:create(x, 20, K.METAL, 128) end
        for x = 0, 39 do s:create(x, 51, K.GLASS, 128) end
    end
    for i = 1, 1500 do
        for _, k in ipairs(plate) do if s.alive[k] == 1 then s.ptmp[k] = 80 end end
        s:step()
    end
    local function layer(y0, y1)
        local sum, n = 0, 0
        for i = 0, s.maxUsed - 1 do
            if s.alive[i] == 1 and s.ptype[i] == K.WATER
               and s.py[i] >= y0 and s.py[i] < y1 then sum = sum + s.ptmp[i]; n = n + 1 end
        end
        return n > 0 and sum / n or -1
    end
    if fromBelow then return layer(20, 30), layer(40, 50)
    else return layer(21, 31), layer(41, 51) end
end
local bTop, bBot = tank(true)
local aTop, aBot = tank(false)
check("нагрев снизу поднимает тепло вверх (конвекция)", bTop > 23,
    string.format("низ %.1f°, верх %.1f°", bBot, bTop))
check("нагрев сверху вниз не идёт (устойчивое расслоение)", aBot < 23,
    string.format("верх %.1f°, низ %.1f°", aTop, aBot))
check("конвекция работает только снизу", (bTop - 22) > (aBot - 22) * 3,
    string.format("прогрев дальнего слоя: снизу %.1f°, сверху %.1f°", bTop - 22, aBot - 22))

-- восходящий поток над огнём
math.randomseed(5)
local sF = Sim.new(60, 90)
for i = 1, 300 do
    if i % 3 == 0 then for x = 28, 32 do sF:create(x, 85, K.FIRE, 128) end end
    sF:step()
end
local aiF = sF.air:index(30, 60)
check("над огнём идёт восходящий поток", sF.air.vy[aiF] < -0.5,
    string.format("скорость воздуха %.2f (минус — вверх), температура %.0f°",
        sF.air.vy[aiF], sF.air.at[aiF]))

----------------------------------------------------------------------
print()
print("=== КИСЛОРОД ===")

local function coalBox(sealed)
    math.randomseed(3)
    local s = Sim.new(48, 48)
    if sealed then
        for d = 0, 3 do
            for x = 10, 38 do s:create(x, 10+d, K.STONE, 128); s:create(x, 38-d, K.STONE, 128) end
            for y = 10, 38 do s:create(10+d, y, K.STONE, 128); s:create(38-d, y, K.STONE, 128) end
        end
    end
    for y = 28, 34 do for x = 15, 33 do s:create(x, y, K.COAL, 128) end end
    for x = 20, 28 do local k = s:create(x, 27, K.FIRE, 128); if k >= 0 then s.ptmp[k] = 900 end end
    local minOx = 9
    for i = 1, 3000 do
        s:step()
        local o = s.air.ox[s.air:index(24, 25)]
        if o < minOx then minOx = o end
    end
    return minOx
end
local oxSealed = coalBox(true)
check("горение выжигает кислород в замкнутом объёме", oxSealed < 0.6,
    string.format("доля кислорода падала до %.2f (обычный воздух 1.00)", oxSealed))

----------------------------------------------------------------------
print()
print("=== ГИДРОСТАТИКА ===")

local function jetSpeed(holeY)
    math.randomseed(2)
    local s = Sim.new(120, 90)
    for y = 10, 85 do s:create(20, y, K.GLASS, 128); s:create(50, y, K.GLASS, 128) end
    for x = 20, 50 do s:create(x, 86, K.GLASS, 128) end
    for y = 12, 85 do for x = 21, 49 do s:create(x, y, K.WATER, 128) end end
    for i = 1, 30 do s:step() end
    s:killAt(50, holeY); s:killAt(50, holeY - 1)
    local vmax = 0
    for i = 1, 200 do
        s:step()
        for k = 0, s.maxUsed - 1 do
            if s.alive[k] == 1 and s.ptype[k] == K.WATER
               and s.px[k] > 51 and s.px[k] < 58 and s.pvx[k] > vmax then vmax = s.pvx[k] end
        end
    end
    return vmax
end
local vDeep, vShallow = jetSpeed(84), jetSpeed(30)
check("струя из глубокой пробоины быстрее", vDeep > vShallow * 1.2,
    string.format("столб 72 клетки: %.2f, столб 18 клеток: %.2f", vDeep, vShallow))

math.randomseed(2)
local sCalm = Sim.new(60, 60)
for x = 0, 59 do sCalm:create(x, 56, K.GLASS, 128) end
for y = 30, 55 do for x = 0, 59 do sCalm:create(x, y, K.WATER, 128) end end
for i = 1, 400 do sCalm:step() end
-- Честная мера покоя — сколько частиц меняет клетку. Остаточные скорости
-- мерить бесполезно: у заснувшей воды они просто заморожены ненулевыми.
local prev = {}
for k = 0, sCalm.maxUsed - 1 do
    if sCalm.alive[k] == 1 then
        prev[k] = math.floor(sCalm.py[k]) * sCalm.w + math.floor(sCalm.px[k])
    end
end
local moves = 0
for i = 1, 100 do
    sCalm:step()
    for k = 0, sCalm.maxUsed - 1 do
        if sCalm.alive[k] == 1 then
            local c = math.floor(sCalm.py[k]) * sCalm.w + math.floor(sCalm.px[k])
            if prev[k] and prev[k] ~= c then moves = moves + 1 end
            prev[k] = c
        end
    end
end
local awakeCalm = 0
for c = 0, sCalm.nchunks - 1 do if sCalm.awake[c] ~= 0 then awakeCalm = awakeCalm + 1 end end
check("спокойная вода замирает, а не кипит", moves == 0 and awakeCalm == 0,
    string.format("переходов между клетками за 100 шагов: %d, бодрствующих кусков %d",
        moves, awakeCalm))

----------------------------------------------------------------------
print()
print("=== ВЯЗКОСТЬ, НАТЯЖЕНИЕ, ПЛАВУЧЕСТЬ ЛЬДА ===")

local function spread(mat, hot)
    math.randomseed(4)
    local s = Sim.new(200, 40)
    local blob = {}
    for y = 10, 19 do for x = 95, 104 do blob[#blob+1] = s:create(x, y, mat, 128) end end
    for i = 1, 400 do
        if hot then
            for _, k in ipairs(blob) do
                if s.alive[k] == 1 and s.ptype[k] == mat then s.ptmp[k] = 1400 end
            end
        end
        s:step()
    end
    local mn, mx = 1e9, -1
    for k = 0, s.maxUsed - 1 do
        if s.alive[k] == 1 and s.ptype[k] == mat then
            if s.px[k] < mn then mn = s.px[k] end
            if s.px[k] > mx then mx = s.px[k] end
        end
    end
    return mx - mn
end
local spWater, spLava, spMerc = spread(K.WATER), spread(K.LAVA, true), spread(K.MERCURY)
check("лава растекается хуже воды (вязкость)", spLava < spWater * 0.8,
    string.format("вода %.0f клеток, лава %.0f", spWater, spLava))
check("ртуть собирается, а не растекается (натяжение)", spMerc < spWater * 0.6,
    string.format("вода %.0f клеток, ртуть %.0f", spWater, spMerc))

math.randomseed(4)
local sIce = Sim.new(60, 60)
for y = 20, 50 do for x = 0, 59 do
    local k = sIce:create(x, y, K.WATER, 128); if k >= 0 then sIce.ptmp[k] = 1 end
end end
for y = 44, 48 do for x = 25, 34 do
    sIce:killAt(x, y)
    local k = sIce:create(x, y, K.ICE, 128); if k >= 0 then sIce.ptmp[k] = -5 end
end end
local iceY0 = avgRow(sIce, K.ICE)
for i = 1, 600 do
    for k = 0, sIce.maxUsed - 1 do
        if sIce.alive[k] == 1 and sIce.ptype[k] == K.WATER and sIce.ptmp[k] > 2 then
            sIce.ptmp[k] = 1
        end
    end
    sIce:step()
end
local iceY1 = avgRow(sIce, K.ICE)
local watY = avgRow(sIce, K.WATER)
check("лёд всплывает (917 легче 1000)", iceY1 and iceY1 < watY - 10,
    string.format("лёд был на %.1f, всплыл на %.1f, вода в среднем %.1f",
        iceY0 or -1, iceY1 or -1, watY or -1))

----------------------------------------------------------------------
print()
print("=== РАСТВОРЕНИЕ СОЛИ ===")

math.randomseed(4)
local sSalt = Sim.new(40, 50)
for y = 25, 40 do for x = 0, 39 do sSalt:create(x, y, K.WATER, 128) end end
for y = 10, 14 do for x = 15, 25 do sSalt:create(x, y, K.SALT, 128) end end
for i = 1, 600 do sSalt:step() end
check("соль растворяется в воде, получается рассол", count(sSalt, K.BRINE) > 20,
    string.format("рассола %d клеток, соли осталось %d",
        count(sSalt, K.BRINE), count(sSalt, K.SALT)))

local function iceWithSalt(salted)
    math.randomseed(4)
    local s = Sim.new(40, 40)
    for x = 0, 39 do s:create(x, 30, K.GLASS, 128) end
    for y = 20, 29 do for x = 10, 29 do
        local k = s:create(x, y, K.ICE, 128); if k >= 0 then s.ptmp[k] = -5 end
    end end
    if salted then for x = 10, 29 do s:create(x, 19, K.SALT, 128) end end
    for i = 1, 800 do s:step() end
    return count(s, K.ICE)
end
local iceBare, iceSalted = iceWithSalt(false), iceWithSalt(true)
check("соль топит лёд в мороз", iceSalted < iceBare * 0.5,
    string.format("без соли осталось %d, с солью %d", iceBare, iceSalted))

----------------------------------------------------------------------
print()
print("=== СОН КУСКОВ МИРА ===")

local s16 = Sim.new(180, 300)
fill(s16, 0, 220, 179, 290, K.SAND)
run(s16, 500)
local awake = 0
for c = 0, s16.nchunks - 1 do if s16.awake[c] ~= 0 then awake = awake + 1 end end
check("улёгшаяся куча засыпает", awake == 0,
    string.format("бодрствует %d из %d", awake, s16.nchunks))

local b2, o2 = integrity(s16)
check("целостность после всего", b2 == 0 and o2 == 0,
    string.format("рассогласований %d, осиротевших %d", b2, o2))

----------------------------------------------------------------------
print()
print(string.format("ИТОГО: прошло %d, провалено %d", pass, fail))
