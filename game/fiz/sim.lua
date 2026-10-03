-- sim.lua — движок на частицах, устроенный как в The Powder Toy.
--
-- Что считается за один шаг:
--   1. heat()  теплопередача с учётом теплоёмкости
--   2. react() превращения со скрытой теплотой, горение, взрывы, кислота
--   3. couple() обмен количеством движения между частицами и воздухом
--   4. air:update() поле давления и ветра
--   5. move()  движение: тяготение по Архимеду, снос ветром, столкновения
--
-- Модуль не зависит от LOVE: физику можно прогонять отдельно.

local ffi = require("ffi")
local bit = require("bit")
local E = require("data.elements")
local RX = require("data.chemrx")
local Air = require("fiz.air")

local Sim = {}
Sim.__index = Sim

local CHUNK = 16
local AWAKE = 6
local GRAV  = 0.14
local MAXV  = 6.0
local ROOM  = E.ROOM_TEMP
local TMIN, TMAX = E.TEMP_MIN, E.TEMP_MAX

local POWDER, LIQUID, GAS, SOLID = E.POWDER, E.LIQUID, E.GAS, E.SOLID

local stateArr, densArr, condArr, fixedArr = E.stateArr, E.densArr, E.condArr, E.fixedArr
local capArr, latFArr, latVArr, expansArr = E.capArr, E.latFArr, E.latVArr, E.expansArr
local gravArr, dragArr, elastArr, jitArr = E.gravArr, E.dragArr, E.elastArr, E.jitArr
local reposeArr, advecArr = E.reposeArr, E.advecArr
local airDragArr, airLossArr, hotAirArr, expandArr = E.airDragArr, E.airLossArr, E.hotAirArr, E.expandArr
local stateOf = E.stateOf
local meltAt, meltTo = E.meltAt, E.meltTo
local boilAt, boilTo = E.boilAt, E.boilTo
local coolAt, coolTo = E.coolAt, E.coolTo
local igniteAt, flamOf, residueOf = E.igniteAt, E.flamOf, E.residueOf
local lifeOf, decayToOf, acidProofOf = E.lifeOf, E.decayToOf, E.acidProofOf
local blastOf, shockOf, burnTempOf = E.blastOf, E.shockOf, E.burnTempOf
local burnLifeOf, burnToOf = E.burnLifeOf, E.burnToOf
local oxyUseOf, oxyNeedOf, activeArr = E.oxyUseOf, E.oxyNeedOf, E.activeArr
local tensionArr, viscArr = E.tensionArr, E.viscArr
local tempOf = E.tempOf

local FIRE, SMOKE, ACID = E.byKey.FIRE, E.byKey.SMOKE, E.byKey.ACID
local WATER, STEAM = E.byKey.WATER, E.byKey.STEAM
local SALT, BRINE, ICE = E.byKey.SALT, E.byKey.BRINE, E.byKey.ICE

local floor, abs, ceil = math.floor, math.abs, math.ceil

-- Собственный генератор случайных чисел (xorshift32). Нужен не ради
-- качества, а ради СВЕРКИ: ядро на C++ обязано выдавать ту же
-- последовательность, иначе сравнивать поведение двух версий нечем,
-- а math.random у LuaJIT и rand() у C++ разные по определению.
local bxor, lsh, rsh = bit.bxor, bit.lshift, bit.rshift
local rngState = 2463534242

local function random(n)
    local st = rngState
    st = bxor(st, lsh(st, 13))
    st = bxor(st, rsh(st, 17))
    st = bxor(st, lsh(st, 5))
    rngState = st
    local u = (st % 4294967296) * (1 / 4294967296)
    if n then return floor(u * n) + 1 end
    return u
end
local rshift, band = bit.rshift, bit.band
local CSHIFT, CMASK = 4, 15     -- CHUNK = 16
local ASHIFT = 2                -- CELL воздуха = 4

local FLOW = 0.12
local BODY_RELAX = 0.0015
local PRESS_BOIL = 2.2          -- на сколько градусов давление поднимает кипение
local AIR_COND   = 0.15         -- предел теплообмена вещества с воздухом
local CAIR       = 1.0          -- теплоёмкость одной ячейки воздуха
local T0ABS      = 295.0        -- комнатная температура в кельвинах
-- Настоящее тепловое расширение воды даёт разницу плотностей в доли
-- процента. В жизни этого хватает для бурной конвекции, потому что вязкость
-- воды ничтожна. У нас вязкость на порядки выше (шаг по времени грубый),
-- и те же доли процента не сдвинут ничего. Усиливаем расширение так, чтобы
-- совпало поведение, а не коэффициент: конвекция должна начинаться при
-- перепаде в несколько градусов, как в чайнике.
local THERMAL_GAIN = 30.0
local SWAP_MIN   = 0.002        -- порог разницы плотностей для обмена местами
local PRESS_K    = 0.30
local GAP_MAX    = 2            -- пузырь такой высоты не разрывает столб жидкости         -- сила гидростатического давления
local PRESS_MAX  = 1.2          -- потолок ускорения от давления
local BLAST_K    = 55.0         -- перевод силы взрыва в давление
-- Скрытая теплота задана в настоящих кДж/кг. Если брать её один к одному,
-- чайник закипает полчаса, поэтому вводим общий понижающий множитель.
local LAT_SCALE  = 0.20

-- Будит кусок, в котором лежит клетка. Соседние куски трогаем только если
-- клетка стоит на границе: иначе на каждую сдвинувшуюся частицу приходилось
-- бы девять записей, а это сотни тысяч записей за кадр.
local function wakeAt(aw, cw, ch, x, y)
    local cx, cy = rshift(x, CSHIFT), rshift(y, CSHIFT)
    local row = cy * cw
    aw[row + cx] = AWAKE
    local mx, my = band(x, CMASK), band(y, CMASK)
    local left  = (mx == 0) and cx > 0
    local right = (mx == CMASK) and cx < cw - 1
    local up    = (my == 0) and cy > 0
    local down  = (my == CMASK) and cy < ch - 1
    if left  then aw[row + cx - 1] = AWAKE end
    if right then aw[row + cx + 1] = AWAKE end
    if up then
        local r = row - cw
        aw[r + cx] = AWAKE
        if left  then aw[r + cx - 1] = AWAKE end
        if right then aw[r + cx + 1] = AWAKE end
    end
    if down then
        local r = row + cw
        aw[r + cx] = AWAKE
        if left  then aw[r + cx - 1] = AWAKE end
        if right then aw[r + cx + 1] = AWAKE end
    end
end

-- Плотность с поправкой на температуру.
-- Газы — по уравнению состояния: горячий воздух во столько же раз легче,
-- во сколько выше его абсолютная температура.
-- Жидкости и твёрдое — по коэффициенту теплового расширения.
local function effDens(t, tp)
    if stateArr[t] == GAS then
        local k = T0ABS / (tp + 273)
        if k > 6 then k = 6 elseif k < 0.05 then k = 0.05 end
        return densArr[t] * k
    end
    local k = 1 - expansArr[t] * THERMAL_GAIN * (tp - 22)
    if k > 1.8 then k = 1.8 elseif k < 0.2 then k = 0.2 end
    return densArr[t] * k
end

----------------------------------------------------------------------
-- Задать затравку генератора: нужна для повторяемых прогонов и сверки.
function Sim.seed(v)
    rngState = (v ~= 0) and v or 2463534242
end

function Sim.new(w, h)
    local self = setmetatable({}, Sim)
    self.w, self.h, self.n = w, h, w * h
    self.maxp = self.n

    -- Тип и остаток после горения — номера веществ. Байта не хватает:
    -- с соединениями веществ больше 255, и мел (259) становился камнем (3).
    self.ptype = ffi.new("uint16_t[?]", self.maxp)
    self.alive = ffi.new("uint8_t[?]", self.maxp)
    self.px    = ffi.new("double[?]", self.maxp)
    self.py    = ffi.new("double[?]", self.maxp)
    self.pvx   = ffi.new("double[?]", self.maxp)
    self.pvy   = ffi.new("double[?]", self.maxp)
    self.ptmp  = ffi.new("double[?]", self.maxp)
    self.ptmp2 = ffi.new("double[?]", self.maxp)
    self.plat  = ffi.new("double[?]", self.maxp)   -- накопленная скрытая теплота
    self.plife = ffi.new("uint8_t[?]", self.maxp)
    self.pshd  = ffi.new("uint8_t[?]", self.maxp)
    self.pdir  = ffi.new("uint8_t[?]", self.maxp)
    self.pres  = ffi.new("uint16_t[?]", self.maxp)
    -- Горение как процесс: пока идёт реакция, вещество само держит свою
    -- температуру. Без этого одиночная раскалённая клетка остывает за
    -- несколько шагов, отдав тепло четырём соседям, и термит гаснет.
    -- Улежалось ли сыпучее. Без этой памяти частица пробует сползти каждый
    -- кадр, и за сотню попыток срабатывает любая вероятность — песок и
    -- гравий становятся неотличимы.
    self.pset   = ffi.new("uint8_t[?]", self.maxp)
    self.pburn  = ffi.new("uint8_t[?]", self.maxp)
    -- Плотность каждой частицы с поправкой на температуру. Раньше она
    -- считалась заново для частицы и обоих её соседей — три деления
    -- на частицу за кадр. Теперь один раз.
    -- Служебные массивы для прохода движения. Состояние подшага держим
    -- в памяти, а не в локальных переменных: иначе у внутреннего цикла
    -- получается полтора десятка переменных, живущих между итерациями,
    -- ассемблер LuaJIT не справляется с распределением регистров
    -- (NYI: PHI shuffling too complex), выбрасывает трассировку и гонит
    -- весь проход интерпретатором. На телефоне это стоило 81.7 мс.
    self.psx   = ffi.new("double[?]", self.maxp)   -- шаг по x за подшаг
    self.phead = ffi.new("double[?]", self.maxp)   -- избыток напора
    self.ptl   = ffi.new("uint8_t[?]", self.maxp)  -- сколько своих соседей
    self.psy   = ffi.new("double[?]", self.maxp)   -- шаг по y за подшаг
    self.phit  = ffi.new("double[?]", self.maxp)   -- скорость удара
    self.pheld = ffi.new("uint8_t[?]", self.maxp)  -- опёрта ли частица
    self.order = ffi.new("int32_t[?]", self.maxp)  -- обход advect снизу вверх
    self.ordcnt = ffi.new("int32_t[?]", self.h + 1)
    self.pdens  = ffi.new("double[?]", self.maxp)
    self.pburnT = ffi.new("double[?]", self.maxp)

    self.pmap = ffi.new("int32_t[?]", self.n)
    -- Гидростатическое давление в жидкости: вес столба над клеткой.
    -- Без него вода выравнивается случайными сдвигами, из пробитой
    -- стенки ничего не бьёт, а сообщающиеся сосуды не работают.
    self.lp = ffi.new("int32_t[?]", self.n)
    -- Чистый вес своего столба, без оглядки на соседей. Разница между
    -- ним и решённым давлением и есть "меня подпирает чужой столб".
    -- Самая высокая точка связной области жидкости, в которой лежит клетка.
    self.ctop = ffi.new("int32_t[?]", self.n)
    -- Верх собственного столба жидкости. Раньше уровень выводился из
    -- накопленного давления, и один пузырёк внутри воды обнулял счёт:
    -- частица под пузырём считала себя поверхностью, видела «отставание»
    -- во всю глубину бака и лезла вверх. Вода кипела сама по себе.
    self.coltop = ffi.new("int32_t[?]", self.n)
    self.rowcnt = ffi.new("int32_t[?]", self.h + 2)   -- столбов на каждой высоте
    self.cstack = ffi.new("int32_t[?]", self.n)
    self.clist = ffi.new("int32_t[?]", self.n)
    self.free = ffi.new("int32_t[?]", self.maxp)

    self.cw = ceil(w / CHUNK)
    self.ch = ceil(h / CHUNK)
    self.nchunks = self.cw * self.ch
    self.awake = ffi.new("uint8_t[?]", self.nchunks)
    self.therm = ffi.new("uint8_t[?]", self.nchunks)
    self.thermTmp = ffi.new("uint8_t[?]", self.nchunks)  -- копия на время расширения метки

    self.air = Air.new(w, h)
    self.acw = self.air.cw

    self:clear()
    return self
end

function Sim:clear()
    ffi.fill(self.pmap, self.n * 4, 0)
    ffi.fill(self.lp, self.n * 4, 0)
    ffi.fill(self.ctop, self.n * 4, 0)
    ffi.fill(self.alive, self.maxp, 0)
    ffi.fill(self.awake, self.nchunks, 0)
    ffi.fill(self.therm, self.nchunks, 0)
    ffi.fill(self.plat, self.maxp * 8, 0)
    ffi.fill(self.pburn, self.maxp, 0)
    ffi.fill(self.pset, self.maxp, 0)
    for i = 0, self.maxp - 1 do self.free[i] = self.maxp - 1 - i end
    self.freeTop = self.maxp
    self.maxUsed = 0
    self.count = 0
    self.air:clear()
end

function Sim:wake(cx, cy)
    local cw, ch, aw = self.cw, self.ch, self.awake
    local x0, x1 = cx - 1, cx + 1
    local y0, y1 = cy - 1, cy + 1
    if x0 < 0 then x0 = 0 end
    if y0 < 0 then y0 = 0 end
    if x1 > cw - 1 then x1 = cw - 1 end
    if y1 > ch - 1 then y1 = ch - 1 end
    for y = y0, y1 do
        local r = y * cw
        for x = x0, x1 do aw[r + x] = AWAKE end
    end
end

function Sim:wakeCell(x, y)
    if x < 0 or y < 0 or x >= self.w or y >= self.h then return end
    wakeAt(self.awake, self.cw, self.ch, x, y)
end

----------------------------------------------------------------------
function Sim:create(x, y, id, rnd)
    if x < 0 or y < 0 or x >= self.w or y >= self.h then return -1 end
    if id == 0 then return -1 end
    local ci = y * self.w + x
    if self.pmap[ci] ~= 0 then return -1 end
    if self.freeTop <= 0 then return -1 end

    self.freeTop = self.freeTop - 1
    local i = self.free[self.freeTop]

    self.ptype[i] = id
    self.alive[i] = 1
    self.px[i] = x + 0.5
    self.py[i] = y + 0.5
    self.pvx[i] = 0
    self.pvy[i] = 0
    self.ptmp[i] = tempOf[id]
    self.plat[i] = 0
    self.plife[i] = lifeOf[id]
    self.pshd[i] = rnd or 128
    self.pdir[i] = (rnd or 0) % 2
    self.pres[i] = 0
    self.pburn[i] = 0
    self.pset[i] = 0

    self.pmap[ci] = i + 1
    if i >= self.maxUsed then self.maxUsed = i + 1 end
    self.count = self.count + 1

    self:wake(floor(x / CHUNK), floor(y / CHUNK))
    local tp = self.ptmp[i]
    if tp > ROOM + 1 or tp < ROOM - 1 then
        self.therm[floor(y / CHUNK) * self.cw + floor(x / CHUNK)] = AWAKE
    end
    return i
end

function Sim:killIndex(i)
    if self.alive[i] == 0 then return end
    local x, y = floor(self.px[i]), floor(self.py[i])
    if x >= 0 and y >= 0 and x < self.w and y < self.h then
        local ci = y * self.w + x
        if self.pmap[ci] == i + 1 then self.pmap[ci] = 0 end
        self:wakeCell(x, y)
    end
    self.alive[i] = 0
    self.free[self.freeTop] = i
    self.freeTop = self.freeTop + 1
    self.count = self.count - 1
end

function Sim:killAt(x, y)
    if x < 0 or y < 0 or x >= self.w or y >= self.h then return end
    local occ = self.pmap[y * self.w + x]
    if occ ~= 0 then self:killIndex(occ - 1) end
end


function Sim:tempAt(x, y)
    if x < 0 or y < 0 or x >= self.w or y >= self.h then return ROOM end
    local occ = self.pmap[y * self.w + x]
    if occ == 0 then return ROOM end
    return self.ptmp[occ - 1]
end

function Sim:pressureAt(x, y)
    return self.air.pv[self.air:index(x, y)]
end

function Sim:oxygenAt(x, y)
    return self.air.ox[self.air:index(x, y)]
end


function Sim:convert(i, id)
    if id == 0 then self:killIndex(i); return end
    self.ptype[i] = id
    self.plife[i] = lifeOf[id]
    self.plat[i] = 0
    self:wakeCell(floor(self.px[i]), floor(self.py[i]))
end

----------------------------------------------------------------------
-- 1. ТЕПЛОПЕРЕДАЧА
----------------------------------------------------------------------
function Sim:heat()
    local w, h = self.w, self.h
    local pmap, ptype, alive = self.pmap, self.ptype, self.alive
    local T, T2 = self.ptmp, self.ptmp2
    local therm, cw, ch = self.therm, self.cw, self.ch
    local px, py = self.px, self.py
    local airT, acw = self.air.at, self.acw
    local mu = self.maxUsed
    if mu == 0 then return end

    ffi.copy(T2, T, mu * 8)

    for i = 0, mu - 1 do
        if alive[i] == 1 then
            local t = ptype[i]
            do
                local x, y = floor(px[i]), floor(py[i])
                if therm[rshift(y, CSHIFT) * cw + rshift(x, CSHIFT)] ~= 0 then
                    local ti = T[i]
                    local ci = condArr[t]
                    local acc = 0
                    local base = y * w + x
                    -- Сколько сторон частицы открыто воздуху. Закопанная
                    -- частица с воздухом не обменивается вовсе.
                    local open = 0

                    local occ = (x > 0) and pmap[base - 1] or -1
                    if occ > 0 then
                        local o = occ - 1
                        local k = condArr[ptype[o]]; if ci < k then k = ci end
                        acc = acc + (T[o] - ti) * k
                    elseif occ == 0 then
                        open = open + 1
                    end

                    occ = (x < w - 1) and pmap[base + 1] or -1
                    if occ > 0 then
                        local o = occ - 1
                        local k = condArr[ptype[o]]; if ci < k then k = ci end
                        acc = acc + (T[o] - ti) * k
                    elseif occ == 0 then
                        open = open + 1
                    end

                    occ = (y > 0) and pmap[base - w] or -1
                    if occ > 0 then
                        local o = occ - 1
                        local k = condArr[ptype[o]]; if ci < k then k = ci end
                        acc = acc + (T[o] - ti) * k
                    elseif occ == 0 then
                        open = open + 1
                    end

                    occ = (y < h - 1) and pmap[base + w] or -1
                    if occ > 0 then
                        local o = occ - 1
                        local k = condArr[ptype[o]]; if ci < k then k = ci end
                        acc = acc + (T[o] - ti) * k
                    elseif occ == 0 then
                        open = open + 1
                    end

                    -- Обмен с воздухом. Воздух — полноценное тело со своей
                    -- температурой: он нагревается от горячего и сам греет
                    -- то, что над огнём. Энергия сохраняется: сколько частица
                    -- взяла, столько воздух отдал.
                    if open > 0 then
                        local ai = rshift(y, ASHIFT) * acw + rshift(x, ASHIFT)
                        local ta = airT[ai]
                        local k = (ci < AIR_COND and ci or AIR_COND) * open * 0.25
                        local q = (ta - ti) * k
                        acc = acc + q
                        airT[ai] = ta - q * FLOW * capArr[t] / CAIR
                    end

                    -- Теплоёмкость: одно и то же тепло поднимает температуру
                    -- воды вчетверо слабее, чем железа. Делитель не опускаем
                    -- ниже единицы, иначе явная схема теряет устойчивость.
                    if fixedArr[t] == 0 then
                        local cp = capArr[t]
                        if cp < 1.0 then cp = 1.0 end
                        local nt = ti + acc * (FLOW / cp)
                        -- Остывание в среду — потеря ПОВЕРХНОСТЬЮ, а не
                        -- всем объёмом. Раньше к комнатной температуре
                        -- подтягивалась каждая частица, в том числе
                        -- внутри сплошного тела: получалась дыра по
                        -- всему объёму, и брусок железа не прогревался
                        -- до конца никогда.
                        if open > 0 then
                            nt = nt + (ROOM - nt) * BODY_RELAX * open * 0.25
                        end
                        if nt < TMIN then nt = TMIN elseif nt > TMAX then nt = TMAX end
                        T2[i] = nt
                    end
                end
            end
        end
    end

    self.ptmp, self.ptmp2 = T2, T

    local T3 = self.ptmp
    local pmap, ptype = self.pmap, self.ptype
    local awake, w = self.awake, self.w
    ffi.fill(therm, self.nchunks, 0)
    for i = 0, mu - 1 do
        if alive[i] == 1 then
            local tv = T3[i]
            if tv > ROOM + 1 or tv < ROOM - 1 then
                local x, y = floor(px[i]), floor(py[i])
                wakeAt(therm, cw, ch, x, y)

                -- Конвекция живёт только в бодрствующем куске: уснувший
                -- бак перестаёт перемешиваться, и тепло замирает слоями.
                -- Поэтому разница температур по вертикали будит движение —
                -- именно она и есть подъёмная сила.
                local st = stateArr[ptype[i]]
                if (st == LIQUID or st == GAS) and y < self.h - 1 then
                    local o = pmap[(y + 1) * w + x]
                    if o ~= 0 then
                        local d = tv - T3[o - 1]
                        if d > 2 or d < -2 then
                            wakeAt(awake, cw, ch, x, y)
                        end
                    end
                end
            end
        end
    end
    -- Расширяем тепловую метку на один кусок во все стороны.
    --
    -- Без этого тепло упирается в невидимую стену: метку получает лишь
    -- частица, которая сама заметно горячее комнатной, а на переднем
    -- крае волны она уже остыла — её кусок засыпает, и дальше тепло не
    -- идёт. Расширение считается по кускам, а не по частицам: кусков
    -- сотни, а частиц десятки тысяч.
    local tt = self.thermTmp
    ffi.copy(tt, therm, self.nchunks)
    for cy = 0, ch - 1 do
        for cx = 0, cw - 1 do
            if tt[cy * cw + cx] ~= 0 then
                local x0 = cx > 0 and cx - 1 or 0
                local x1 = cx < cw - 1 and cx + 1 or cw - 1
                local y0 = cy > 0 and cy - 1 or 0
                local y1 = cy < ch - 1 and cy + 1 or ch - 1
                for y = y0, y1 do
                    for x = x0, x1 do therm[y * cw + x] = AWAKE end
                end
            end
        end
    end

end

----------------------------------------------------------------------
-- Взрыв: давление в воздух плюс поджог и разрушение вокруг.
----------------------------------------------------------------------
function Sim:explode(x, y, power)
    self.air:blast(x, y, power * BLAST_K)
    local r = ceil(power * 1.6)
    if r > 6 then r = 6 end
    local w, h, pmap, ptype = self.w, self.h, self.pmap, self.ptype
    for dy = -r, r do
        for dx = -r, r do
            local d2 = dx * dx + dy * dy
            if d2 <= r * r then
                local nx, ny = x + dx, y + dy
                if nx >= 0 and ny >= 0 and nx < w and ny < h then
                    local occ = pmap[ny * w + nx]
                    if occ ~= 0 then
                        local oi = occ - 1
                        local ot = ptype[oi]
                        if fixedArr[ot] == 0 then
                            self.ptmp[oi] = self.ptmp[oi] + 600 * (1 - d2 / (r * r + 1))
                            local sp = 2.0 * power / (1 + d2)
                            if sp > 4.0 then sp = 4.0 end
                            self.pvx[oi] = self.pvx[oi] + dx * sp
                            self.pvy[oi] = self.pvy[oi] + dy * sp
                        end
                    end
                    self:wakeCell(nx, ny)
                end
            end
        end
    end
end

----------------------------------------------------------------------
-- 2. ПРЕВРАЩЕНИЯ
----------------------------------------------------------------------
function Sim:react()
    local w, h = self.w, self.h
    local pmap, ptype, alive = self.pmap, self.ptype, self.alive
    local T, life, lat = self.ptmp, self.plife, self.plat
    local pburn, pburnT, pres = self.pburn, self.pburnT, self.pres
    local px, py = self.px, self.py
    local air, acw = self.air, self.acw
    local pv, aox = air.pv, air.ox
    local mu = self.maxUsed

    for i = 0, mu - 1 do
        if alive[i] == 1 then
            local t = ptype[i]
            local tp = T[i]
            local lf = lifeOf[t]

            if lf > 0 or activeArr[t] == 1 or t == ACID or tp > ROOM + 1 or tp < ROOM - 1 then
                local x, y = floor(px[i]), floor(py[i])
                local base = y * w + x

                if t == FIRE then
                    local ai = rshift(y, ASHIFT) * acw + rshift(x, ASHIFT)
                    local o2 = aox[ai]
                    -- Горение сжигает кислород. В запаянной банке огонь
                    -- гаснет сам, а у приоткрытой щели держится.
                    local use = oxyUseOf[FIRE]
                    o2 = o2 - use
                    if o2 < 0 then o2 = 0 end
                    aox[ai] = o2
                    local wet = false
                    if x > 0 then local o = pmap[base - 1]
                        if o ~= 0 and ptype[o - 1] == WATER then wet = true end end
                    if not wet and x < w - 1 then local o = pmap[base + 1]
                        if o ~= 0 and ptype[o - 1] == WATER then wet = true end end
                    if not wet and y > 0 then local o = pmap[base - w]
                        if o ~= 0 and ptype[o - 1] == WATER then wet = true end end
                    if not wet and y < h - 1 then local o = pmap[base + w]
                        if o ~= 0 and ptype[o - 1] == WATER then wet = true end end
                    if wet and random() < 0.70 then
                        -- залило: пламя гаснет, остаётся пар
                        if random() < 0.5 then
                            self:convert(i, STEAM); T[i] = 110
                        else
                            self:killIndex(i)
                        end
                    elseif o2 < oxyNeedOf[FIRE] then
                        -- Задохнулся. Без кислорода древесина не сгорает,
                        -- а обугливается: остаётся то, что осталось бы
                        -- после горения. Так и делают древесный уголь —
                        -- жгут дрова в яме без доступа воздуха.
                        local res = pres[i]
                        if res > 0 then
                            self:convert(i, res)
                        elseif random() < 0.35 then
                            self:convert(i, SMOKE)
                        else
                            self:killIndex(i)
                        end
                    elseif tp < 600 then
                        tp = tp + (900 - tp) * 0.5
                        T[i] = tp
                    end
                end

                -- идёт реакция горения: вещество держит свою температуру
                local bn = pburn[i]
                if bn > 0 then
                    local bt = pburnT[i]
                    if tp < bt then tp = bt; T[i] = bt end
                    pburn[i] = bn - 1
                end

                -- срок жизни
                if lf > 0 then
                    local l = life[i]
                    if l > 0 then
                        life[i] = l - 1
                    else
                        if t == FIRE then
                            local res = pres[i]
                            if res > 0 and random() < 0.30 then
                                self:convert(i, res)
                            elseif random() < 0.35 then
                                self:convert(i, SMOKE)
                            else
                                self:killIndex(i)
                            end
                        else
                            local to = decayToOf[t]
                            if to == 0 then self:killIndex(i) else self:convert(i, to) end
                        end
                    end
                end

                if alive[i] == 1 then
                    t = ptype[i]
                    local cp = capArr[t]
                    local st = stateOf[t]

                    -- Давление поднимает точку кипения, разрежение опускает.
                    -- В вакууме вода закипает холодной — как в жизни.
                    local bo = boilAt[t]
                    if bo < 99999 then
                        bo = bo + pv[air:index(x, y)] * PRESS_BOIL
                    end

                    if tp >= bo then
                        -- Скрытая теплота парообразования: пока она не набрана,
                        -- температура стоит на точке кипения и не растёт.
                        lat[i] = lat[i] + (tp - bo) * cp
                        T[i] = bo
                        if lat[i] >= latVArr[t] * LAT_SCALE then
                            lat[i] = 0
                            -- Из выкипевшего рассола соль никуда не девается:
                            -- улетает вода, остаётся осадок.
                            if t == BRINE and random() < 0.25 then
                                self:convert(i, SALT); T[i] = bo
                            else
                                self:convert(i, boilTo[t])
                                T[i] = bo + 2
                            end
                        end
                    elseif tp >= meltAt[t] then
                        lat[i] = lat[i] + (tp - meltAt[t]) * cp
                        T[i] = meltAt[t]
                        if lat[i] >= latFArr[t] * LAT_SCALE then
                            lat[i] = 0
                            self:convert(i, meltTo[t])
                            T[i] = meltAt[t] + 2
                        end
                    elseif tp <= coolAt[t] then
                        -- обратный переход: отдаём ту же скрытую теплоту
                        local need = ((st == GAS) and latVArr[t] or latFArr[t]) * LAT_SCALE
                        lat[i] = lat[i] + (coolAt[t] - tp) * cp
                        T[i] = coolAt[t]
                        if lat[i] >= need then
                            lat[i] = 0
                            self:convert(i, coolTo[t])
                            T[i] = coolAt[t] - 2
                        end
                    else
                        local lv = lat[i]
                        if lv ~= 0 then lat[i] = lv * 0.97 end

                        -- Соль растворяется в воде. Получается рассол:
                        -- он тяжелее воды, замерзает при минус восьми
                        -- и при выкипании оставляет соль обратно.
                        if t == SALT then
                            local j
                            local r = random(4)
                            if r == 1 and x > 0 then j = base - 1
                            elseif r == 2 and x < w - 1 then j = base + 1
                            elseif r == 3 and y > 0 then j = base - w
                            elseif r == 4 and y < h - 1 then j = base + w end
                            if j then
                                local o = pmap[j]
                                if o ~= 0 and ptype[o - 1] == WATER and random() < 0.12 then
                                    self:convert(o - 1, BRINE)
                                    self:killIndex(i)
                                end
                            end
                        elseif t == ICE then
                            -- Соль понижает точку замерзания: посыпанный
                            -- солью лёд тает и в мороз. Так чистят дороги.
                            local melted = false
                            if x > 0 then local o = pmap[base - 1]
                                if o ~= 0 then local n2 = ptype[o - 1]
                                    if n2 == SALT or n2 == BRINE then melted = true end end end
                            if not melted and x < w - 1 then local o = pmap[base + 1]
                                if o ~= 0 then local n2 = ptype[o - 1]
                                    if n2 == SALT or n2 == BRINE then melted = true end end end
                            if not melted and y > 0 then local o = pmap[base - w]
                                if o ~= 0 then local n2 = ptype[o - 1]
                                    if n2 == SALT or n2 == BRINE then melted = true end end end
                            if not melted and y < h - 1 then local o = pmap[base + w]
                                if o ~= 0 then local n2 = ptype[o - 1]
                                    if n2 == SALT or n2 == BRINE then melted = true end end end
                            if melted and tp > -8 and random() < 0.03 then
                                self:convert(i, WATER)
                            end
                        elseif (t == WATER or t == BRINE) and tp > 35 then
                            -- Испарение с открытой поверхности: лужа сохнет
                            -- задолго до кипения, и тем быстрее, чем теплее.
                            if y > 0 and pmap[base - w] == 0
                               and random() < (tp - 35) * 0.00012 then
                                if t == BRINE and random() < 0.25 then
                                    self:convert(i, SALT)
                                else
                                    self:convert(i, STEAM); T[i] = tp + 20
                                end
                            end
                        end

                        if flamOf[t] > 0 then
                            local hot = tp >= igniteAt[t]
                            local touch = false
                            if not hot then
                                -- Поджигает не только пламя, но и любой
                                -- раскалённый сосед: расплав, лава, калёный
                                -- металл. Через одну теплопроводность это
                                -- не работает — сосед успевает остыть.
                                local ign = igniteAt[t]
                                if x > 0 then
                                    local o = pmap[base - 1]
                                    if o ~= 0 then
                                        local oi2 = o - 1
                                        if ptype[oi2] == FIRE or T[oi2] >= ign then touch = true end
                                    end
                                end
                                if not touch and x < w - 1 then
                                    local o = pmap[base + 1]
                                    if o ~= 0 then
                                        local oi2 = o - 1
                                        if ptype[oi2] == FIRE or T[oi2] >= ign then touch = true end
                                    end
                                end
                                if not touch and y > 0 then
                                    local o = pmap[base - w]
                                    if o ~= 0 then
                                        local oi2 = o - 1
                                        if ptype[oi2] == FIRE or T[oi2] >= ign then touch = true end
                                    end
                                end
                                if not touch and y < h - 1 then
                                    local o = pmap[base + w]
                                    if o ~= 0 then
                                        local oi2 = o - 1
                                        if ptype[oi2] == FIRE or T[oi2] >= ign then touch = true end
                                    end
                                end
                            end
                            local ai2 = rshift(y, ASHIFT) * acw + rshift(x, ASHIFT)
                            if (hot or touch) and aox[ai2] >= oxyNeedOf[t]
                               and random() < flamOf[t] * (hot and 1 or 5) then
                                local res = residueOf[t]
                                local bl = blastOf[t]
                                local bt = burnTempOf[t]
                                pres[i] = res >= 0 and res or 0
                                -- Чаще всего горючее превращается в пламя.
                                -- Но термит даёт расплав: газ улетел бы вверх
                                -- и ничего не прожёг, а расплав остаётся лежать.
                                local bTo = burnToOf[t]
                                if bTo < 0 then bTo = FIRE end
                                self:convert(i, bTo)
                                T[i] = bt
                                local bLife = burnLifeOf[t]
                                if bLife > 0 then
                                    self.plife[i] = bLife > 255 and 255 or bLife
                                    pburn[i] = bLife > 255 and 255 or bLife
                                    pburnT[i] = bt
                                end
                                self:wakeCell(x, y)
                                if bl > 0 then self:explode(x, y, bl) end
                            end
                        elseif t == ACID then
                            local j
                            local r = random(4)
                            if r == 1 and x > 0 then j = base - 1
                            elseif r == 2 and x < w - 1 then j = base + 1
                            elseif r == 3 and y > 0 then j = base - w
                            elseif r == 4 and y < h - 1 then j = base + w end
                            if j then
                                local o = pmap[j]
                                if o ~= 0 then
                                    local oi = o - 1
                                    local nb = ptype[oi]
                                    if nb ~= ACID and acidProofOf[nb] == 0 and random() < 0.25 then
                                        self:killIndex(oi)
                                        if random() < 0.12 then self:killIndex(i) end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
end


----------------------------------------------------------------------
-- 2б. ХИМИЯ: соединение веществ и разложение при нагреве
----------------------------------------------------------------------
-- Вся химия лежит в готовой таблице пар (см. chemrx.lua), поэтому здесь
-- на частицу приходится одно обращение по числовому ключу. Перебирать
-- правила в кадре нельзя: на 25 тысячах частиц это дороже всей физики.
local rxIdx, rxList = RX.pairIdx, RX.pairList
local rxDecIdx, rxDecList = RX.decIdx, RX.decList
local rxAct, RXN = RX.actArr, RX.N

function Sim:mix()
    local w, h = self.w, self.h
    local pmap, ptype, alive = self.pmap, self.ptype, self.alive
    local T, px, py = self.ptmp, self.px, self.py
    local mu = self.maxUsed

    for i = 0, mu - 1 do
        if alive[i] == 1 and rxAct[ptype[i]] == 1 then
            local t = ptype[i]
            local tp = T[i]
            local x, y = floor(px[i]), floor(py[i])
            local base = y * w + x

            local di = rxDecIdx[t]
            local d = di ~= 0 and rxDecList[di] or nil
            if d and tp >= d.t and random() < d.p then
                -- Разложение: сама частица становится первым продуктом,
                -- второй садится в свободную соседнюю клетку. Если места
                -- нет, второй продукт теряется — зато сетка не ломается.
                self:convert(i, d.o1)
                if d.o2 then
                    local nx, ny = x, y
                    local r = random(4)
                    if     r == 1 then nx = x - 1
                    elseif r == 2 then nx = x + 1
                    elseif r == 3 then ny = y - 1
                    else                ny = y + 1 end
                    if nx >= 0 and nx < w and ny >= 0 and ny < h
                       and pmap[ny * w + nx] == 0 then
                        -- create возвращает -1, когда частица не влезла:
                        -- писать температуру по этому индексу нельзя
                        local j = self:create(nx, ny, d.o2)
                        if j >= 0 then T[j] = tp end
                    end
                end
                self:wakeCell(x, y)
            else
                local j
                local r = random(4)
                if     r == 1 and x > 0     then j = base - 1
                elseif r == 2 and x < w - 1 then j = base + 1
                elseif r == 3 and y > 0     then j = base - w
                elseif r == 4 and y < h - 1 then j = base + w end

                if j then
                    local o = pmap[j]
                    if o ~= 0 then
                        local oi = o - 1
                        local pi = rxIdx[t * RXN + ptype[oi]]
                        local rc = pi ~= 0 and rxList[pi] or nil
                        if rc and tp >= rc.t and tp <= rc.tmax
                           and random() < rc.p then
                            local ok = true
                            if rc.cat then
                                -- катализатор сам не расходуется, но без
                                -- него реакция не идёт: аммиак без железа
                                -- не получить, окисление без платины тоже
                                ok = false
                                local cat = rc.cat
                                if x > 0 then local q = pmap[base - 1]
                                    if q ~= 0 and cat[ptype[q - 1]] then ok = true end end
                                if not ok and x < w - 1 then local q = pmap[base + 1]
                                    if q ~= 0 and cat[ptype[q - 1]] then ok = true end end
                                if not ok and y > 0 then local q = pmap[base - w]
                                    if q ~= 0 and cat[ptype[q - 1]] then ok = true end end
                                if not ok and y < h - 1 then local q = pmap[base + w]
                                    if q ~= 0 and cat[ptype[q - 1]] then ok = true end end
                            end
                            if ok then
                                local heat = rc.heat
                                local o1, o2 = rc.o1, rc.o2
                                if o1 >= 0 then
                                    self:convert(i, o1)
                                    local nt = tp + heat
                                    if nt > E.TEMP_MAX then nt = E.TEMP_MAX end
                                    if nt < E.TEMP_MIN then nt = E.TEMP_MIN end
                                    T[i] = nt
                                else
                                    self:killIndex(i)
                                end
                                if o2 >= 0 then
                                    self:convert(oi, o2)
                                    local nt = T[oi] + heat
                                    if nt > E.TEMP_MAX then nt = E.TEMP_MAX end
                                    if nt < E.TEMP_MIN then nt = E.TEMP_MIN end
                                    T[oi] = nt
                                else
                                    self:killIndex(oi)
                                end
                                self:wakeCell(x, y)
                            end
                        end
                    end
                end
            end
        end
    end
end

----------------------------------------------------------------------
-- 3. ОБМЕН С ВОЗДУХОМ
----------------------------------------------------------------------
function Sim:couple()
    local alive, ptype = self.alive, self.ptype
    local px, py, pvx, pvy, T = self.px, self.py, self.pvx, self.pvy, self.ptmp
    local air = self.air
    local avx, avy, apv, awall = air.vx, air.vy, air.pv, air.wall
    local acw, achh = air.cw, air.chh
    local mu = self.maxUsed
    ffi.fill(awall, air.n * 8, 0)

    for i = 0, mu - 1 do
        if alive[i] == 1 then
            local t = ptype[i]
            local ax = rshift(floor(px[i]), ASHIFT)
            local ay = rshift(floor(py[i]), ASHIFT)
            if ax >= 0 and ay >= 0 and ax < acw and ay < achh then
                local ai = ay * acw + ax
                local loss = airLossArr[t]
                local dragc = airDragArr[t]
                -- Частица тормозит воздух в своей клетке и толкает его за собой.
                -- У твёрдого loss равен нулю: оно глушит ветер, как стена.
                avx[ai] = avx[ai] * loss + dragc * pvx[i]
                avy[ai] = avy[ai] * loss + dragc * pvy[i]
                local ha = hotAirArr[t]
                if ha ~= 0 then apv[ai] = apv[ai] + ha end
                local ex = expandArr[t]
                if ex ~= 0 then
                    -- нагретый газ расширяется и давит
                    apv[ai] = apv[ai] + (T[i] - ROOM) * ex
                end
                -- Заполненность ячейки воздуха веществом. Ячейка вчетверо
                -- крупнее клетки мира, поэтому стенка толщиной в одну клетку
                -- занимает лишь четверть ячейки. Считать её четвертью стены
                -- нельзя: сквозь неё пошёл бы кислород. Четырёх клеток
                -- твёрдого — то есть сплошной перегородки — достаточно,
                -- чтобы ячейка считалась глухой.
                local st2 = stateArr[t]
                if st2 == SOLID then awall[ai] = awall[ai] + 0.25
                elseif st2 == LIQUID then awall[ai] = awall[ai] + 0.10
                elseif st2 == POWDER then awall[ai] = awall[ai] + 0.06 end
            end
        end
    end
end

----------------------------------------------------------------------
-- Вес столба жидкости над каждой клеткой. Один проход по столбцам:
-- накопленный вес сбрасывается на любой непрозрачной для жидкости границе.
----------------------------------------------------------------------
function Sim:liquidPressure()
    local w, h = self.w, self.h
    local pmap, ptype, lp, ctop = self.pmap, self.ptype, self.lp, self.ctop

    -- Шаг 1. Вес столба жидкости над каждой клеткой. Отсюда берутся
    -- боковая сила (струя из пробоины) и местный уровень поверхности.
    local coltop = self.coltop
    for x = 0, w - 1 do
        local acc = 0
        local top = -1
        local gap = 0
        local i = x
        for y = 0, h - 1 do
            local o = pmap[i]
            if o ~= 0 and stateArr[ptype[o - 1]] == LIQUID then
                -- Пузырь в один-два ряда столб не разрывает: над ним
                -- всё та же вода, и её вес никуда не делся.
                if top < 0 or gap > GAP_MAX then
                    top = y
                    acc = 0
                end
                gap = 0
                local d = densArr[ptype[o - 1]]
                lp[i] = acc + d * 0.5
                acc = acc + d
                ctop[i] = -1              -- пометка «ещё не разобрано»
                coltop[i] = top
            else
                gap = gap + 1
                if gap > GAP_MAX then
                    acc = 0
                    top = -1
                end
                lp[i] = 0
                ctop[i] = 32000
                coltop[i] = y
            end
            i = i + w
        end
    end

    -- Шаг 2. Связные области жидкости. Вода в одной области сообщается,
    -- значит её уровень обязан быть общим. Для каждой области находим
    -- самую высокую точку и запоминаем её во всех её клетках.
    -- Это и есть закон сообщающихся сосудов: решать его через поле
    -- давления бесполезно — статическое решение просто размазывает
    -- перепад и не выражает того, что уровни ещё не сравнялись.
    local stack = self.cstack
    local coltop = self.coltop
    for y0 = 0, h - 1 do
        local row0 = y0 * w
        for x0 = 0, w - 1 do
            local start = row0 + x0
            if ctop[start] == -1 then
                local sp = 0
                stack[sp] = start; sp = sp + 1
                ctop[start] = -2          -- взято в обработку
                local cells = self.clist
                local nc = 0
                cells[nc] = start; nc = nc + 1
                local minY = y0
                while sp > 0 do
                    sp = sp - 1
                    local c = stack[sp]
                    local cy = (c - c % w) / w
                    local cx = c % w
                    if cy < minY then minY = cy end
                    if cx > 0 and ctop[c - 1] == -1 then
                        ctop[c - 1] = -2; stack[sp] = c - 1; sp = sp + 1
                        cells[nc] = c - 1; nc = nc + 1
                    end
                    if cx < w - 1 and ctop[c + 1] == -1 then
                        ctop[c + 1] = -2; stack[sp] = c + 1; sp = sp + 1
                        cells[nc] = c + 1; nc = nc + 1
                    end
                    if cy > 0 and ctop[c - w] == -1 then
                        ctop[c - w] = -2; stack[sp] = c - w; sp = sp + 1
                        cells[nc] = c - w; nc = nc + 1
                    end
                    if cy < h - 1 and ctop[c + w] == -1 then
                        ctop[c + w] = -2; stack[sp] = c + w; sp = sp + 1
                        cells[nc] = c + w; nc = nc + 1
                    end
                end
                -- Уровень области. Брать самую верхнюю клетку нельзя:
                -- одна подскочившая брызга поднимает «уровень» на десяток
                -- клеток, и вся вода в баке начинает считать, что отстаёт,
                -- и лезть вверх. Новые брызги — новый повод, вода кипит
                -- без остановки. Поэтому берём не крайний столб, а нижнюю
                -- четверть: отдельные капли в неё не попадают.
                local rc = self.rowcnt
                local ntop = 0
                for k = 0, nc - 1 do
                    local c = cells[k]
                    local cy = (c - c % w) / w
                    if coltop[c] == cy then
                        rc[cy] = (rc[cy] or 0) + 1
                        ntop = ntop + 1
                    end
                end
                local need = ntop * 0.25
                if need < 1 then need = 1 end
                local acc2, level = 0, minY
                for yy = minY, h - 1 do
                    local v = rc[yy]
                    if v ~= 0 then
                        acc2 = acc2 + v
                        if acc2 >= need then level = yy; break end
                    end
                end
                for yy = minY, h - 1 do rc[yy] = 0 end
                for k = 0, nc - 1 do ctop[cells[k]] = level end
            end
        end
    end
end

----------------------------------------------------------------------
-- 4. ДВИЖЕНИЕ
----------------------------------------------------------------------
function Sim:clampWall()
    local awall, n = self.air.wall, self.air.n
    for i = 0, n - 1 do
        local v = awall[i]
        if v > 0.97 then awall[i] = 0.97 end
    end
end

-- Пересчёт плотностей с поправкой на температуру, раз за кадр.
function Sim:densities()
    local alive, ptype, T, pd = self.alive, self.ptype, self.ptmp, self.pdens
    for i = 0, self.maxUsed - 1 do
        if alive[i] == 1 then pd[i] = effDens(ptype[i], T[i]) end
    end
end

-- Проход 1: силы. Считаем ускорения и записываем скорость,
-- избыток напора и число своих соседей. Отдельной функцией —
-- чтобы трассировка JIT получилась короткой: в одной петле
-- на 400 строк ассемблер не раскладывает значения по регистрам
-- и выбрасывает её целиком.
function Sim:forces()
    local w, h = self.w, self.h
    local pmap, ptype, alive = self.pmap, self.ptype, self.alive
    local px, py, pvx, pvy = self.px, self.py, self.pvx, self.pvy
    local pdir, pset = self.pdir, self.pset
    local awake, cw, ch = self.awake, self.cw, self.ch
    local air = self.air
    local avx, avy = air.vx, air.vy
    local acw = air.cw
    local pd = self.pdens
    local adens = air.adens
    local lp, ctop = self.lp, self.ctop
    local mu = self.maxUsed

    local phead, ptl = self.phead, self.ptl
    for i = 0, mu - 1 do
        if alive[i] == 1 then
            local t = ptype[i]
            local st = stateArr[t]

            if st ~= SOLID then
                local x, y = px[i], py[i]
                local ox, oy = floor(x), floor(y)
                local chunk = rshift(oy, CSHIFT) * cw + rshift(ox, CSHIFT)

                if awake[chunk] ~= 0 then
                    local dt = densArr[t]
                    local curI = oy * w + ox

                    -- Закон Архимеда с поправкой на температуру.
                    -- Газы: уравнение состояния, плотность обратна абсолютной
                    -- температуре — горячий воздух вчетверо легче холодного.
                    -- Жидкости и твёрдое: тепловое расширение по справочному
                    -- коэффициенту. Отсюда сама собой берётся конвекция:
                    -- нагретая снизу вода всплывает, остывшая опускается.
                    local dSelf = pd[i]
                    if dSelf < 0.1 then dSelf = 0.1 end

                    local md, mc = 0, 0
                    local aIdx = rshift(oy, ASHIFT) * acw + rshift(ox, ASHIFT)
                    local dAir = adens[aIdx]
                    if oy > 0 then
                        local o = pmap[curI - w]
                        if o == 0 then
                            md = md + dAir; mc = mc + 1
                        else
                            local oi3 = o - 1
                            local os = stateArr[ptype[oi3]]
                            if os == LIQUID or os == GAS then
                                md = md + pd[oi3]; mc = mc + 1
                            end
                        end
                    end
                    if oy < h - 1 then
                        local o = pmap[curI + w]
                        if o == 0 then
                            md = md + dAir; mc = mc + 1
                        else
                            local oi3 = o - 1
                            local os = stateArr[ptype[oi3]]
                            if os == LIQUID or os == GAS then
                                md = md + pd[oi3]; mc = mc + 1
                            end
                        end
                    end
                    local medium = (mc > 0) and (md / mc) or dAir

                    -- вес минус выталкивающая сила, в долях от обычного веса
                    local gEff = 1 - medium / dSelf
                    if gEff > 1 then gEff = 1 elseif gEff < -1.5 then gEff = -1.5 end
                    -- Жидкость в самой себе была бы невесомой и перестала бы
                    -- оседать. Оставляем небольшой вес — но только если она
                    -- не всплывает: иначе задавили бы тепловую конвекцию.
                    if st == LIQUID and gEff >= 0 and gEff < 0.15 then gEff = 0.15 end

                    local vx = pvx[i] * dragArr[t]
                    local vy = (pvy[i] + GRAV * gEff * gravArr[t]) * dragArr[t]

                    -- Боковое давление столба. По вертикали давление уже
                    -- учтено через Архимеда, поэтому берём только разницу
                    -- слева и справа: она и выгоняет струю из пробоины
                    -- и выравнивает сообщающиеся сосуды.
                    local headExcess = 0
                    local tenLike = 4
                    if st == LIQUID then
                        -- Вязкость как настоящее трение, а не как вероятность:
                        -- вероятность за сотню кадров всё равно срабатывает,
                        -- и лава растекалась бы не хуже воды.
                        local vs = viscArr[t]
                        if vs > 0 then vx = vx * (1 - vs) end
                        -- Поверхностное натяжение. Капля тянется к своим:
                        -- если с одной стороны соседей больше, туда и тянет.
                        -- Если соседей мало совсем, капля цепляется за себя
                        -- и не растекается плёнкой. Отсюда шарики ртути.
                        local tn = tensionArr[t]
                        if tn > 0 then
                            local lf, rf, uf, df = 0, 0, 0, 0
                            if ox > 0 then local o = pmap[curI - 1]
                                if o ~= 0 and ptype[o - 1] == t then lf = 1 end end
                            if ox < w - 1 then local o = pmap[curI + 1]
                                if o ~= 0 and ptype[o - 1] == t then rf = 1 end end
                            if oy > 0 then local o = pmap[curI - w]
                                if o ~= 0 and ptype[o - 1] == t then uf = 1 end end
                            if oy < h - 1 then local o = pmap[curI + w]
                                if o ~= 0 and ptype[o - 1] == t then df = 1 end end
                            tenLike = lf + rf + uf + df
                            vx = vx + tn * 0.30 * (lf - rf)
                            if tenLike <= 2 then vx = vx * (1 - tn) end
                        end
                        local selfP = lp[curI]
                        local pL, pR = selfP, selfP
                        if ox > 0 then
                            local o = pmap[curI - 1]
                            if o == 0 then pL = 0
                            else
                                local os2 = stateArr[ptype[o - 1]]
                                if os2 == LIQUID then pL = lp[curI - 1]
                                elseif os2 == GAS then pL = 0 end
                            end
                        end
                        if ox < w - 1 then
                            local o = pmap[curI + 1]
                            if o == 0 then pR = 0
                            else
                                local os2 = stateArr[ptype[o - 1]]
                                if os2 == LIQUID then pR = lp[curI + 1]
                                elseif os2 == GAS then pR = 0 end
                            end
                        end
                        local ap = PRESS_K * GRAV * (pL - pR) / (2 * dSelf)
                        if ap > PRESS_MAX then ap = PRESS_MAX
                        elseif ap < -PRESS_MAX then ap = -PRESS_MAX end
                        vx = vx + ap
                        -- Насколько здешний уровень ниже общего уровня всей
                        -- связной области. В ровном сосуде это ноль.
                        -- Во втором колене сообщающихся сосудов — разница
                        -- высот, и ровно она поднимает воду.
                        -- Уровень собственного столба берём напрямую,
                        -- а не выводим из давления: вывод через давление
                        -- ломался от любого пузыря.
                        headExcess = self.coltop[curI] - ctop[curI]
                        -- Избыток давления толкает воду вверх. Это та самая
                        -- сила, что поднимает воду во втором колене и бьёт
                        -- фонтаном из трубы под напором.
                        if headExcess > 0.5 then
                            -- Отстающее колено сообщающихся сосудов. Вес
                            -- столба в покое уравновешен давлением снизу:
                            -- поднимает воду не добавка к тяжести, а сам
                            -- перекос уровней. Поэтому тяжесть для этой
                            -- частицы на шаг снимается (её держит столб
                            -- под ней), а вместо неё ставится ускорение
                            -- U-образной трубки: a = g·Δh/L, где L — длина
                            -- пути воды, то есть перекос плюс глубина
                            -- колена. При Δh = L это ровно свободное
                            -- падение, больше не бывает.
                            local depth = h - self.coltop[curI]
                            local L = headExcess + depth
                            if L < 1 then L = 1 end
                            local au = GRAV * headExcess / L
                            vy = vy - GRAV * gEff * gravArr[t] * dragArr[t]
                            vy = vy - au * dragArr[t]
                        end
                    end

                    -- Снос ветром. Сила сопротивления воздуха пропорциональна
                    -- плотности воздуха, а ускорение от неё — обратно плотности
                    -- самой частицы. Поэтому дым ветром несёт, а воду и расплав
                    -- почти нет. Раньше скорость ветра добавлялась напрямую,
                    -- и восходящий поток от собственного жара поднимал лаву
                    -- вверх вместо того, чтобы дать ей упасть.
                    local adv = advecArr[t]
                    if adv > 0 then
                        local ai = rshift(oy, ASHIFT) * acw + rshift(ox, ASHIFT)
                        local ratio = dAir / dSelf
                        if ratio > 1 then ratio = 1 end
                        local kk = adv * ratio
                        vx = vx + kk * (avx[ai] - vx)
                        vy = vy + kk * (avy[ai] - vy)
                    end

                    local j = jitArr[t]
                    if j > 0 then
                        vx = vx + (random() - 0.5) * j
                        vy = vy + (random() - 0.5) * j * 0.5
                    end
                    -- Потолок скорости свой для каждого состояния. Каждая
                    -- пройденная клетка — отдельная проверка столкновения,
                    -- поэтому быстрая жидкость обходится дорого, а бежать
                    -- быстрее трёх клеток за шаг ей незачем.
                    local vmax = (st == LIQUID) and 3.0 or MAXV
                    if vx > vmax then vx = vmax elseif vx < -vmax then vx = -vmax end
                    if vy > vmax then vy = vmax elseif vy < -vmax then vy = -vmax end

                    pvx[i] = vx
                    pvy[i] = vy
                    phead[i] = headExcess
                    ptl[i] = tenLike
                end
            end
        end
    end
end

-- Проход 2: перенос и столкновения.
-- Опора: передаётся снизу вверх по столбцу. Неопёртая частица падает,
-- а значит не имеет права спать — в уснувшем куске силы не считаются.
function Sim:support()
    local w, h = self.w, self.h
    local pmap, ptype, pheld = self.pmap, self.ptype, self.pheld
    local awake, cw, ch = self.awake, self.cw, self.ch
    for x = 0, w - 1 do
        local below = true                 -- под нижним рядом край мира
        for y = h - 1, 0, -1 do
            local o = pmap[y * w + x]
            if o == 0 then
                below = false
            else
                local i = o - 1
                local t = ptype[i]
                local st = stateArr[t]
                local fixedBody = (fixedArr[t] ~= 0) or (st == SOLID)
                pheld[i] = (fixedBody or below) and 1 or 0
                below = pheld[i] ~= 0
                if pheld[i] == 0 and not fixedBody
                   and (st == POWDER or st == LIQUID) then
                    wakeAt(awake, cw, ch, x, y)
                end
            end
        end
    end
end

function Sim:advect()
    local w, h = self.w, self.h
    local pmap, ptype, alive = self.pmap, self.ptype, self.alive
    local px, py, pvx, pvy = self.px, self.py, self.pvx, self.pvy
    local pdir, pset = self.pdir, self.pset
    local awake, cw, ch = self.awake, self.cw, self.ch
    local air = self.air
    local pd = self.pdens
    local mu = self.maxUsed
    local psx, psy, phit = self.psx, self.psy, self.phit
    local phead, ptl = self.phead, self.ptl
    local pheld = self.pheld

    -- Стопка падающих держит сама себя: верхняя не сдвинется, пока не
    -- освободит клетку нижняя. Разбираем стопку снизу — тогда вся
    -- цепочка съезжает за один шаг и пласт не расползается.
    local order, rowcnt = self.order, self.ordcnt
    for y = 0, h do rowcnt[y] = 0 end
    for i = 0, mu - 1 do
        if alive[i] == 1 and stateArr[ptype[i]] ~= SOLID then
            local yy = floor(py[i])
            if yy < 0 then yy = 0 elseif yy >= h then yy = h - 1 end
            rowcnt[yy] = rowcnt[yy] + 1
        end
    end
    local acc = 0
    for y = h - 1, 0, -1 do
        local c = rowcnt[y]; rowcnt[y] = acc; acc = acc + c
    end
    local nord = acc
    for i = 0, mu - 1 do
        if alive[i] == 1 and stateArr[ptype[i]] ~= SOLID then
            local yy = floor(py[i])
            if yy < 0 then yy = 0 elseif yy >= h then yy = h - 1 end
            order[rowcnt[yy]] = i; rowcnt[yy] = rowcnt[yy] + 1
        end
    end

    for oIdx = 0, nord - 1 do
        local i = order[oIdx]
        if alive[i] == 1 then
            local t = ptype[i]
            local st = stateArr[t]

            if st ~= SOLID then
                local x, y = px[i], py[i]
                local ox, oy = floor(x), floor(y)
                local chunk = rshift(oy, CSHIFT) * cw + rshift(ox, CSHIFT)

                if awake[chunk] ~= 0 then
                    local dt = densArr[t]
                    local vx, vy = pvx[i], pvy[i]
                    local headExcess = phead[i]
                    local tenLike = ptl[i]
                    local dSelf = pd[i]
                    if dSelf < 0.1 then dSelf = 0.1 end

                    local el = elastArr[t]
                    local sp = abs(vx); local q = abs(vy); if q > sp then sp = q end
                    local steps = ceil(sp)
                    if steps < 1 then steps = 1 end
                    if steps > 7 then steps = 7 end

                    -- Состояние подшага держим в массивах, а не в локальных
                    -- переменных. Подробности у psx/psy в Sim.new: иначе
                    -- ассемблер LuaJIT не раскладывает по регистрам полтора
                    -- десятка живущих между итерациями значений и выбрасывает
                    -- трассировку, после чего весь проход идёт интерпретатором.
                    px[i] = x; py[i] = y
                    pvx[i] = vx; pvy[i] = vy
                    psx[i] = vx / steps; psy[i] = vy / steps
                    phit[i] = 0
                    local ox0, oy0 = ox, oy

                    for s = 1, steps do
                        local xx, yy = px[i], py[i]
                        local sx, sy = psx[i], psy[i]
                        local cx, cy = floor(xx), floor(yy)
                        local cI = cy * w + cx
                        local nx, ny = xx + sx, yy + sy
                        local tx, ty = floor(nx), floor(ny)

                        if tx == cx and ty == cy then
                            px[i] = nx; py[i] = ny
                        else
                            if tx < 0 or tx >= w then
                                pvx[i] = -pvx[i] * el
                                sx = -sx; psx[i] = sx
                                if tx < 0 then nx = 0.01 else nx = w - 0.01 end
                                tx = floor(nx)
                            end
                            if ty < 0 or ty >= h then
                                pvy[i] = -pvy[i] * el
                                sy = -sy; psy[i] = sy
                                if ty < 0 then ny = 0.01 else ny = h - 0.01 end
                                ty = floor(ny)
                            end

                            local nI = ty * w + tx
                            local occ = pmap[nI]

                            if occ == 0 then
                                pmap[cI] = 0
                                pmap[nI] = i + 1
                                px[i] = nx; py[i] = ny
                                pset[i] = 0
                            else
                                local oi = occ - 1
                                local ot = ptype[oi]
                                local ost = stateArr[ot]

                                local swap = false
                                if ost == LIQUID or ost == GAS then
                                    local odE = pd[oi]
                                    local lim = dSelf * SWAP_MIN
                                    if sy > 0 and odE < dSelf - lim then swap = true
                                    elseif sy < 0 and odE > dSelf + lim then swap = true
                                    elseif ost == GAS and st ~= GAS then swap = true end
                                end

                                if swap then
                                    px[oi] = cx + 0.5; py[oi] = cy + 0.5
                                    pmap[cI] = oi + 1
                                    pmap[nI] = i + 1
                                    -- вязкий обмен количеством движения
                                    pvx[oi] = pvx[oi] * 0.5 + pvx[i] * 0.2
                                    pvy[oi] = pvy[oi] * 0.5 + pvy[i] * 0.2
                                    pvx[i] = pvx[i] * 0.7; pvy[i] = pvy[i] * 0.7
                                    psx[i] = sx * 0.7; psy[i] = sy * 0.7
                                    px[i] = nx; py[i] = ny
                                else
                                    phit[i] = abs(pvx[i]) + abs(pvy[i])
                                    -- Удар бывает только при сближении: если
                                    -- сосед уходит от нас не медленнее, чем мы
                                    -- идём к нему, касания нет, клетка занята
                                    -- лишь оттого, что мир нарезан на клетки.
                                    local rel = (tx - cx) * (pvx[i] - pvx[oi])
                                              + (ty - cy) * (pvy[i] - pvy[oi])
                                    local mobile = (fixedArr[ot] == 0)
                                               and (ost ~= SOLID)
                                    local closing = rel > 0
                                    local freePair = mobile and pheld[oi] == 0
                                                 and pheld[i] == 0
                                    if freePair and closing then
                                        -- неупругий удар с сохранением импульса
                                        local m1, m2 = pd[i], pd[oi]
                                        local ms = m1 + m2
                                        local ux = (m1 * pvx[i] + m2 * pvx[oi]) / ms
                                        local uy = (m1 * pvy[i] + m2 * pvy[oi]) / ms
                                        pvx[i] = ux;  pvy[i] = uy
                                        pvx[oi] = ux; pvy[oi] = uy
                                    end
                                    -- Удар расталкивает улежавшееся. Подвижное
                                    -- вещество осыпается от любого толчка,
                                    -- неподвижное держит форму.
                                    if ost == POWDER and pset[oi] == 1 and phit[i] > 0.4
                                       and random() < reposeArr[ot] then
                                        pset[oi] = 0
                                        wakeAt(awake, cw, ch, tx, ty)
                                    end
                                    local slid = false

                                    -- Вода под избыточным напором поднимается:
                                    -- меняется местами с тем, что над ней.
                                    -- Подниматься может только верхняя частица
                                    -- столба: именно она переливается в соседнее
                                    -- колено. Если позволить всплывать и толще,
                                    -- бак бурлит сам по себе.
                                    if st == LIQUID and headExcess > 2 and cy > 0
                                       and self.coltop[cI] == cy then
                                        local up = pmap[cI - w]
                                        if up ~= 0 then
                                            local ui = up - 1
                                            if stateArr[ptype[ui]] == LIQUID then
                                                px[ui] = cx + 0.5
                                                py[ui] = cy + 0.5
                                                pmap[cI] = ui + 1
                                                pmap[cI - w] = i + 1
                                                -- частица переехала на клетку
                                                -- вверх: дальнейшее скольжение
                                                -- считается уже от неё
                                                cI = cI - w
                                                cy = cy - 1
                                                px[i] = cx + 0.5
                                                py[i] = cy + 0.5
                                                pvy[i] = -0.2
                                                slid = true
                                            end
                                        end
                                    end

                                    -- Угол естественного откоса: сыпучее
                                    -- соскальзывает не всегда.
                                    -- Скольжение требует опоры: в свободном
                                    -- падении кучи нет и скатываться не с чего.
                                    local freeFall = freePair and (not closing)
                                    local maySlide = (st == POWDER or st == LIQUID)
                                                 and (not freeFall)
                                    if st == POWDER then
                                        -- подвижность 0 — это льдина: падает,
                                        -- но вбок не растекается
                                        if reposeArr[t] == 0 then maySlide = false end
                                    elseif maySlide then
                                        -- Вязкость: густая жидкость растекается
                                        -- неохотно.
                                        if random() < viscArr[t] then
                                            maySlide = false
                                        else
                                            -- Поверхностное натяжение: капля,
                                            -- у которой мало своих соседей,
                                            -- стягивается, а не растекается.
                                            local tn = tensionArr[t]
                                            if tn > 0 and tenLike <= 2 and random() < tn then
                                                maySlide = false
                                            end
                                        end
                                    end
                                    if maySlide then
                                        local pref = (pdir[i] == 0) and -1 or 1
                                        for k = 1, 2 do
                                            local sdx = (k == 1) and pref or -pref
                                            local cxx = cx + sdx
                                            if cxx >= 0 and cxx < w then
                                                local tries = (st == LIQUID) and 2 or 1
                                                for tt = 1, tries do
                                                    local cyy = (tt == 1) and (cy + 1) or cy
                                                    if cyy >= 0 and cyy < h then
                                                        local dI = cyy * w + cxx
                                                        local co = pmap[dI]
                                                        local ok = false
                                                        if co == 0 then
                                                            ok = true
                                                        else
                                                            local c3 = ptype[co - 1]
                                                            local cs = stateArr[c3]
                                                            if (cs == LIQUID or cs == GAS)
                                                               and densArr[c3] < dt then ok = true end
                                                        end
                                                        -- Крутой обрыв осыпается всегда,
                                                        -- пологий склон — по подвижности.
                                                        if ok and st == POWDER then
                                                            local deep = (cyy + 1 < h)
                                                                and pmap[(cyy + 1) * w + cxx] == 0
                                                            if not deep then
                                                                if pset[i] == 1 then
                                                                    ok = false
                                                                elseif random() >= reposeArr[t] then
                                                                    pset[i] = 1
                                                                    ok = false
                                                                end
                                                            end
                                                        end
                                                        if ok then
                                                            if co ~= 0 then
                                                                local oi2 = co - 1
                                                                px[oi2] = cx + 0.5
                                                                py[oi2] = cy + 0.5
                                                                pmap[cI] = oi2 + 1
                                                            else
                                                                pmap[cI] = 0
                                                            end
                                                            pmap[dI] = i + 1
                                                            px[i] = cxx + 0.5
                                                            py[i] = cyy + 0.5
                                                            if cyy > cy and freePair then
                                                                -- скатывание вбок: касание
                                                                -- сдвигает частицу, но не
                                                                -- отбирает у неё падение
                                                                local fr = 1 - 0.25 * viscArr[t]
                                                                pvy[i] = pvy[i] * fr
                                                                pvx[i] = pvx[i] * fr + sdx * 0.05
                                                            else
                                                                pvx[i] = sdx * (abs(pvy[i]) * 0.4 + 0.25)
                                                                     * (1 - viscArr[t])
                                                                pvy[i] = pvy[i] * 0.3
                                                            end
                                                            slid = true
                                                            break
                                                        end
                                                    end
                                                end
                                            end
                                            if slid then break end
                                        end
                                        if not slid then pdir[i] = 1 - pdir[i] end
                                    elseif st == GAS then
                                        local sdx = (random() < 0.5) and -1 or 1
                                        local cxx = cx + sdx
                                        if cxx >= 0 and cxx < w and pmap[cy * w + cxx] == 0 then
                                            pmap[cI] = 0
                                            pmap[cy * w + cxx] = i + 1
                                            px[i] = cxx + 0.5
                                            pvx[i] = sdx * 0.4
                                            slid = true
                                        end
                                    end

                                    -- Отскок бывает только от опоры.
                                    if (not slid) and (not freeFall)
                                       and not (freePair and closing) then
                                        pvx[i] = pvx[i] * 0.4
                                        pvy[i] = -pvy[i] * el
                                    end
                                    break
                                end
                            end
                        end
                    end

                    ox = floor(px[i]); oy = floor(py[i])
                    local movedCell = (ox ~= ox0) or (oy ~= oy0)
                    local hitSpeed = phit[i]

                    -- взрыв от удара: нитроглицерин не прощает падений
                    local sh = shockOf[t]
                    if sh > 0 and hitSpeed > sh then
                        local bl = blastOf[t]
                        self.pres[i] = 0
                        self:convert(i, FIRE)
                        self.ptmp[i] = 1200
                        self:explode(ox, oy, bl)
                    elseif movedCell then
                        wakeAt(awake, cw, ch, ox, oy)
                    end
                end
            end
        end
    end

    local aw = self.awake
    for c = 0, self.nchunks - 1 do
        local v = aw[c]
        if v ~= 0 then aw[c] = v - 1 end
    end
end

function Sim:move()
    self:forces()
    self:advect()
end

function Sim:step()
    self:support()
    self:liquidPressure()
    self:heat()
    self:react()
    self:mix()
    self:densities()
    self:couple()
    self:clampWall()
    self.air:update()
    self:move()
end

-- Тот же генератор наружу: нужен опытам, которые сверяются с C++ и
-- обязаны брать числа из общего потока, а не из своего.
Sim.random = random

Sim.CHUNK = CHUNK
return Sim
