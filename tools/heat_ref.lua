-- Тот же опыт на эталоне (Lua), числа печатаются в том же порядке.
local Sim = require("fiz.sim")
local E   = require("data.elements")
local s = Sim.new(60, 40)
for y = 10, 25 do for x = 5, 50 do s:create(x, y, E.byKey.WATER) end end
for x = 5, 50 do s:create(x, 26, E.byKey.METAL) end
for y = 10, 26 do s:create(4, y, E.byKey.STONE) end
for x = 20, 30 do
    local o = s.pmap[26 * 60 + x]
    if o ~= 0 then s.ptmp[o - 1] = 500 end
end
for c = 0, s.nchunks - 1 do s.therm[c] = 6 end
for i = 1, 200 do s:heat() end
local sum, mx, mn, n = 0, -1e9, 1e9, 0
for i = 0, s.maxUsed - 1 do
    if s.alive[i] == 1 then
        local t = s.ptmp[i]
        sum = sum + t; n = n + 1
        if t > mx then mx = t end
        if t < mn then mn = t end
    end
end
print(string.format("Lua   частиц %d сумма %.6f среднее %.6f макс %.6f мин %.6f", n, sum, sum/n, mx, mn))
local probes = {{25,26},{25,24},{25,20},{25,15},{10,20}}
for _, p in ipairs(probes) do
    local o = s.pmap[p[2] * 60 + p[1]]
    print(string.format("Lua   проба %2d,%2d = %.6f", p[1], p[2], o ~= 0 and s.ptmp[o-1] or 0))
end
