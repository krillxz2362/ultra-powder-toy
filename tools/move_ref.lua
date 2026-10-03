local Sim = require("fiz.sim"); local E = require("data.elements")
Sim.seed(2463534242)
local s = Sim.new(60, 40)
for x = 0, 59 do s:create(x, 39, E.byKey.STONE) end
for y = 5, 14 do for x = 25, 34 do s:create(x, y, E.byKey.SAND) end end
for i = 1, 300 do s:densities(); s:forces(); s:advect() end
local sx, sy, n, lowest, settled = 0, 0, 0, 0, 0
for i = 0, s.maxUsed - 1 do
    if s.alive[i] == 1 and s.ptype[i] == E.byKey.SAND then
        sx = sx + s.px[i]; sy = sy + s.py[i]; n = n + 1
        local y = math.floor(s.py[i]); if y > lowest then lowest = y end
        if s.pset[i] == 1 then settled = settled + 1 end
    end
end
print(string.format("Lua   песчинок %d сумма x %.6f сумма y %.6f низ %d улеглось %d", n, sx, sy, lowest, settled))
io.write("Lua   профиль:")
for x = 18, 41, 3 do
    local top = 40
    for y = 0, 39 do
        local o = s.pmap[y * 60 + x]
        if o ~= 0 and s.ptype[o - 1] == E.byKey.SAND then top = y break end
    end
    io.write(" ", top)
end
print()
