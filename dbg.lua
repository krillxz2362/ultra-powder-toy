package.path = "/home/user/sandbox/game/?.lua;" .. package.path
local E = require("elements"); local Grid = require("grid"); local physics = require("physics")
local K = E.byKey
math.randomseed(5)

print("-- металл: температура вдоль прутка --")
local g = Grid.new(40, 60)
for x = 0, 39 do g:set(x, 30, K.METAL, 128) end
g:set(0, 30, K.HEAT, 128)
for n = 1, 4 do
  for i = 1, 100 do physics.step(g) end
  local s = {}
  for x = 0, 12 do s[#s+1] = string.format("%d", g.temp[30*g.w + x]) end
  print(string.format("после %3d шагов: %s", n*100, table.concat(s, " ")))
end
print("проводимость металла в массиве FFI:", E.condArr[K.METAL], "у нагревателя:", E.condArr[K.HEAT])
print("нагреватель fixed:", E.fixedArr[K.HEAT], " его температура:", g.temp[30*g.w+0])

print()
print("-- огонь на дереве --")
local g2 = Grid.new(40, 60)
for y = 40, 50 do for x = 10, 30 do g2:set(x, y, K.WOOD, 128) end end
for y = 38, 39 do for x = 18, 22 do g2:set(x, y, K.FIRE, 128) end end
for n = 1, 6 do
  for i = 1, 10 do physics.step(g2) end
  local f, wd = 0, 0
  for i = 0, g2.n-1 do
    if g2.typ[i] == K.FIRE then f = f + 1 elseif g2.typ[i] == K.WOOD then wd = wd + 1 end
  end
  print(string.format("шаг %2d: огня %d, дерева %d, темп. верхнего дерева %d",
      n*10, f, wd, g2.temp[40*g2.w + 20]))
end
print("flam дерева:", E.flamOf[K.WOOD], " порог:", E.igniteAt[K.WOOD], " FIRE id:", K.FIRE)
