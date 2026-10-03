package.path = "/home/user/sandbox/game/?.lua;" .. package.path
local E = require("data.elements"); local Sim = require("fiz.sim")
local K = E.byKey
math.randomseed(7)

local function bench(s, name, N)
    local tp, th, tr, ta, tm = 0, 0, 0, 0, 0
    for i = 1, N do
        local z = os.clock(); s:liquidPressure()
        local a = os.clock(); s:heat()
        local b = os.clock(); s:react()
        local c = os.clock(); s:densities(); s:couple(); s:clampWall(); s.air:update()
        local d = os.clock(); s:move()
        local e = os.clock()
        tp = tp + (a-z); th = th + (b-a); tr = tr + (c-b); ta = ta + (d-c); tm = tm + (e-d)
    end
    local P, H, R, A, M = tp*1000/N, th*1000/N, tr*1000/N, ta*1000/N, tm*1000/N
    print(string.format("%-24s давл %.2f тепло %.2f реакц %.2f воздух %.2f движ %.2f = %.2f мс (%.0f к/с)",
        name, P, H, R, A, M, P+H+R+A+M, 1000/(P+H+R+A+M)))
end

-- такой же размер сетки, как на телефоне в альбомной ориентации
local W, H = 408, 97
print(string.format("сетка %dx%d = %d клеток", W, H, W*H))

local a = Sim.new(W, H)
bench(a, "ПУСТОЙ МИР", 200)

local b = Sim.new(W, H)
for y = 40, 96 do for x = 0, W-1 do b:create(x, y, K.SAND, math.random(0,255)) end end
for i = 1, 300 do b:step() end
bench(b, string.format("УЛЁГШИЙСЯ ПЕСОК %d", b.count), 200)

local c = Sim.new(W, H)
for y = 0, 70 do for x = 0, W-1 do
    local r = math.random()
    if r < 0.5 then c:create(x, y, K.SAND, math.random(0,255))
    elseif r < 0.8 then c:create(x, y, K.WATER, 128) end
end end
bench(c, string.format("ВСЁ В ДВИЖЕНИИ %d", c.count), 120)

local d = Sim.new(W, H)
for y = 0, 90 do for x = 0, W-1 do
    local r = math.random()
    if r < 0.3 then d:create(x, y, K.LAVA, 128)
    elseif r < 0.5 then d:create(x, y, K.WATER, 128)
    elseif r < 0.6 then d:create(x, y, K.ICE, 128) end
end end
bench(d, string.format("ХУДШИЙ СЛУЧАЙ %d", d.count), 80)

-- большой бак воды: гидростатика и связные области в полную силу
local e = Sim.new(W, H)
for y = 40, 96 do for x = 0, W-1 do e:create(x, y, K.WATER, 128) end end
for i = 1, 200 do e:step() end
bench(e, string.format("БАК ВОДЫ %d", e.count), 150)
