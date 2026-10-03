package.path = "/home/user/sandbox/game/?.lua;" .. package.path
local E = require("elements"); local Grid = require("grid"); local physics = require("physics")
local K = E.byKey
math.randomseed(11)

-- одна дощечка, над ней неподвижный огонь: вызываем только проход превращений
local g = Grid.new(8, 8)
g:set(4, 4, K.WOOD, 128)
g:set(4, 3, K.FIRE, 128)
print("до:  дерево =", g.typ[4*8+4], " огонь сверху =", g.typ[3*8+4], " FIRE id =", K.FIRE)
print("flamOf[WOOD] =", E.flamOf[K.WOOD], " igniteAt =", E.igniteAt[K.WOOD],
      " boilAt =", E.boilAt[K.WOOD], " meltAt =", E.meltAt[K.WOOD], " coolAt =", E.coolAt[K.WOOD])
print("температура дощечки =", g.temp[4*8+4])
for i = 1, 40 do
    physics.react(g)
    g.life[3*8+4] = 50          -- не даём огню догореть, он тут только как источник
    if g.typ[4*8+4] ~= K.WOOD then
        print("загорелось на шаге", i, " стало:", g.typ[4*8+4])
        break
    end
end
print("после 40 проходов дощечка =", g.typ[4*8+4], "(4 — всё ещё дерево)")
