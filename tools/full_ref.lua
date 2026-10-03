-- Полный шаг мира: все фазы разом. Печка, вода, дрова, кислота, соль
-- и лёд — чтобы задеть горение, кипение, химию, воздух и растворение.
local Sim = require("fiz.sim"); local E = require("data.elements")
Sim.seed(4242)
local W, H = 90, 60
local s = Sim.new(W, H)
local K = E.byKey
for x = 0, W-1 do s:create(x, H-1, K.STONE) end
for x = 10, 40 do s:create(x, 40, K.STONE) end          -- под печки
for y = 30, 39 do for x = 12, 24 do s:create(x, y, K.WOOD) end end   -- дрова
for x = 14, 20 do s:create(x, 29, K.FIRE) end           -- поджиг
for y = 20, 38 do for x = 55, 80 do s:create(x, y, K.WATER) end end  -- бак
for y = 10, 14 do for x = 60, 66 do s:create(x, y, K.SALT) end end
for y = 10, 14 do for x = 70, 76 do s:create(x, y, K.ICE) end end
for y = 44, 50 do for x = 30, 36 do s:create(x, y, K.ACID) end end
for i = 1, 400 do s:step() end
local n, sx, sy, stp = 0, 0, 0, 0
local cnt = {}
for i = 0, s.maxUsed-1 do
    if s.alive[i] == 1 then
        n = n + 1; sx = sx + s.px[i]; sy = sy + s.py[i]; stp = stp + s.ptmp[i]
        local t = s.ptype[i]; cnt[t] = (cnt[t] or 0) + 1
    end
end
local ap, aox, aat = 0, 0, 0
for i = 0, s.air.n-1 do ap = ap + s.air.pv[i]; aox = aox + s.air.ox[i]; aat = aat + s.air.at[i] end
print(string.format("Lua  частиц %d  x %.6f  y %.6f  тепло %.6f", n, sx, sy, stp))
print(string.format("Lua  воздух: давление %.6f кислород %.6f температура %.6f", ap, aox, aat))
local keys = {}
for t in pairs(cnt) do keys[#keys+1] = t end
table.sort(keys)
-- E.list — обычный массив с единицы, а номер вещества лежит в поле id:
-- брать E.list[id] нельзя, список сдвинут.
local byId = {}
for _, e in ipairs(E.list) do byId[e.id] = e.key end
local out = {}
for _, t in ipairs(keys) do out[#out+1] = string.format("%s=%d", byId[t] or ("?" .. t), cnt[t]) end
print("Lua  состав: " .. table.concat(out, " "))
