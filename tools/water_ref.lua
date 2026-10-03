-- Сообщающиеся сосуды: вода налита только в левое колено, должна
-- перетечь через перемычку и сравнять уровни в обоих.
local Sim = require("fiz.sim"); local E = require("data.elements")
Sim.seed(777)
local W, H = 80, 50
local s = Sim.new(W, H)
local ST, WA = E.byKey.STONE, E.byKey.WATER
for x = 0, W-1 do s:create(x, H-1, ST) end          -- дно
for y = 20, H-2 do s:create(0, y, ST); s:create(W-1, y, ST) end  -- бока
for y = 20, H-12 do s:create(40, y, ST) end          -- перемычка, не до верха
for y = 21, H-2 do for x = 1, 39 do s:create(x, y, WA) end end   -- вода слева
for i = 1, 600 do
    s:liquidPressure(); s:densities(); s:forces(); s:advect()
end
local n, sx, sy = 0, 0, 0
local left, right = 0, 0
for i = 0, s.maxUsed-1 do
    if s.alive[i] == 1 and s.ptype[i] == WA then
        n = n + 1; sx = sx + s.px[i]; sy = sy + s.py[i]
        if s.px[i] < 40 then left = left + 1 else right = right + 1 end
    end
end
-- Верхняя клетка воды в каждом колене: уровни должны сойтись.
local function topAt(x)
    for y = 0, H-1 do
        local o = s.pmap[y*W + x]
        if o ~= 0 and s.ptype[o-1] == WA then return y end
    end
    return -1
end
print(string.format("Lua  воды %d сумма x %.6f сумма y %.6f слева %d справа %d", n, sx, sy, left, right))
print(string.format("Lua  верх: x=10 -> %d   x=70 -> %d", topAt(10), topAt(70)))
