package.path = "/home/user/sandbox/game/?.lua;" .. package.path
local E = require("elements"); local Grid = require("grid"); local physics = require("physics")
local K = E.byKey
math.randomseed(3)
local g = Grid.new(40, 60)
for y = 40, 50 do for x = 10, 30 do g:set(x, y, K.WOOD, 128) end end
for y = 38, 39 do for x = 18, 22 do g:set(x, y, K.FIRE, 128) end end

local function row(y)
    local s = {}
    for x = 16, 24 do
        local t = g.typ[y*g.w+x]
        s[#s+1] = (t == 0 and "." or (t == K.FIRE and "F" or (t == K.WOOD and "W" or (t == K.SMOKE and "s" or tostring(t)))))
    end
    return table.concat(s)
end
for i = 1, 12 do
    physics.step(g)
    print(string.format("шаг %2d  стр37[%s] стр38[%s] стр39[%s] стр40[%s]  темп40=%d  жизнь огня=%d",
        i, row(37), row(38), row(39), row(40), g.temp[40*g.w+20], g.life[39*g.w+20]))
end
