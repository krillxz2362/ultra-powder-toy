-- Перегонка: куб с брагой греем, пар уходит вверх и попадает на
-- холодную стенку, где снова становится жидкостью.
local Sim = require("fiz.sim"); local E = require("data.elements")
Sim.seed(31337)
local W, H = 70, 60
local s = Sim.new(W, H)
local K = E.byKey
local SPIRIT, VAPOR, WATER = K.C_C2H5OH, K.C_C2H5OH_G, K.WATER
-- куб: каменные стенки, крышка с отверстием справа
for x = 5, 35 do s:create(x, 45, K.STONE) end
for y = 25, 44 do s:create(5, y, K.STONE); s:create(35, y, K.STONE) end
for x = 5, 30 do s:create(x, 25, K.STONE) end
-- брага: спирт пополам с водой
for y = 35, 44 do for x = 6, 34 do
    s:create(x, y, (x % 2 == 0) and SPIRIT or WATER)
end end
-- холодильник: каменная плита выше и правее отверстия
for x = 36, 60 do s:create(x, 20, K.STONE) end
for y = 20, 40 do s:create(60, y, K.STONE) end
local function heat(t)
    for x = 6, 34 do
        local o = s.pmap[44 * W + x]
        if o ~= 0 then s.ptmp[o - 1] = t end
    end
end
local function chill()
    for x = 36, 60 do local o = s.pmap[20 * W + x]; if o ~= 0 then s.ptmp[o - 1] = 5 end end
    for y = 20, 40 do local o = s.pmap[y * W + 60]; if o ~= 0 then s.ptmp[o - 1] = 5 end end
end
local function count(id)
    local n = 0
    for i = 0, s.maxUsed - 1 do
        if s.alive[i] == 1 and s.ptype[i] == id then n = n + 1 end
    end
    return n
end
print(string.format("начало:  спирт %d  вода %d  пар %d", count(SPIRIT), count(WATER), count(VAPOR)))
for i = 1, 1200 do
    heat(90)        -- плита держит 90 градусов: выше кипения спирта (78),
    chill()         -- но ниже кипения воды — так и отделяют спирт
    s:step()
end
-- сколько спирта оказалось снаружи куба, то есть перегналось
local out = 0
for i = 0, s.maxUsed - 1 do
    if s.alive[i] == 1 and (s.ptype[i] == SPIRIT or s.ptype[i] == VAPOR) then
        if s.px[i] > 35 then out = out + 1 end
    end
end
print(string.format("Lua  конец: спирт %d  вода %d  пар %d  перегнано за куб %d",
    count(SPIRIT), count(WATER), count(VAPOR), out))
