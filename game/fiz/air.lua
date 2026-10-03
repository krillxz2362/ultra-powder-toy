-- air.lua — поле воздуха: давление, ветер, температура и кислород.
--
-- Считается на огрублённой сетке: одна ячейка воздуха на 4x4 клетки мира.
-- Этого хватает, потому что воздух меняется плавно, а считать его на полной
-- сетке было бы вчетверо дороже без видимой разницы.
--
-- Порядок одного шага:
--   1. подъёмная сила: тёплый воздух идёт вверх (приближение Буссинеска);
--   2. перепад давления разгоняет воздух;
--   3. поток сносит сам себя (полулагранжев перенос) — отсюда вихри;
--   4. поток сносит температуру и кислород;
--   5. расхождение потока меняет давление;
--   6. размытие убирает шахматную рябь;
--   7. потери и условия на краях мира.
--
-- Температура воздуха — это то, чего не хватало для настоящей конвекции.
-- Без неё дым поднимается только потому, что ему прописана малая плотность,
-- а не потому, что его несёт восходящий поток от огня.

local ffi = require("ffi")

local Air = {}
Air.__index = Air

local CELL = 4

local TSTEPP = 0.30     -- давление -> скорость
local TSTEPV = 0.40     -- скорость -> давление
local ADV    = 0.30     -- доля переноса потока самим собой
local PADV   = 0.25     -- доля переноса давления потоком (инерция волны)
local SADV   = 0.60     -- доля переноса примесей (тепла, кислорода)
local VLOSS  = 0.994    -- затухание ветра
local PLOSS  = 0.992    -- затухание давления
local PMAX   = 256.0
local BUOY   = 0.00075  -- подъёмная сила на градус перегрева
local TRELAX = 0.004    -- остывание воздуха к комнатной температуре
local TDIFF  = 0.14     -- расползание тепла по воздуху
local ODIFF  = 0.10     -- расползание кислорода
local ROOM   = 22

local floor = math.floor

function Air.new(w, h)
    local self = setmetatable({}, Air)
    self.cw = math.ceil(w / CELL)
    self.chh = math.ceil(h / CELL)
    self.n = self.cw * self.chh

    self.pv  = ffi.new("double[?]", self.n)
    self.vx  = ffi.new("double[?]", self.n)
    self.vy  = ffi.new("double[?]", self.n)
    self.at  = ffi.new("double[?]", self.n)   -- температура воздуха
    self.ox  = ffi.new("double[?]", self.n)   -- доля кислорода, 1 — обычный воздух
    self.opv = ffi.new("double[?]", self.n)
    self.ovx = ffi.new("double[?]", self.n)
    self.ovy = ffi.new("double[?]", self.n)
    self.oat = ffi.new("double[?]", self.n)
    self.oox = ffi.new("double[?]", self.n)
    -- Доля ячейки, занятая веществом. Сквозь каменную стену не должны
    -- просачиваться ни тепло, ни кислород, ни давление.
    self.wall = ffi.new("double[?]", self.n)
    -- Плотность воздуха с поправкой на температуру. Считается раз за кадр:
    -- в цикле по частицам это деление обходилось слишком дорого.
    self.adens = ffi.new("double[?]", self.n)
    self:clear()
    return self
end

function Air:clear()
    ffi.fill(self.pv, self.n * 8, 0)
    ffi.fill(self.vx, self.n * 8, 0)
    ffi.fill(self.vy, self.n * 8, 0)
    ffi.fill(self.wall, self.n * 8, 0)
    for i = 0, self.n - 1 do
        self.at[i] = ROOM
        self.ox[i] = 1.0
        self.adens[i] = 12
    end
end

function Air:index(x, y)
    local cx, cy = floor(x / CELL), floor(y / CELL)
    if cx < 0 then cx = 0 elseif cx > self.cw - 1 then cx = self.cw - 1 end
    if cy < 0 then cy = 0 elseif cy > self.chh - 1 then cy = self.chh - 1 end
    return cy * self.cw + cx
end

function Air:blast(x, y, power)
    local cw, chh = self.cw, self.chh
    local cx, cy = floor(x / CELL), floor(y / CELL)
    local pv = self.pv
    for dy = -1, 1 do
        for dx = -1, 1 do
            local nx, ny = cx + dx, cy + dy
            if nx >= 0 and ny >= 0 and nx < cw and ny < chh then
                local k = (dx == 0 and dy == 0) and 1.0 or 0.45
                local i = ny * cw + nx
                local v = pv[i] + power * k
                if v > PMAX then v = PMAX end
                pv[i] = v
            end
        end
    end
end

----------------------------------------------------------------------
function Air:update()
    local cw, chh, n = self.cw, self.chh, self.n
    local pv, vx, vy, at, ox = self.pv, self.vx, self.vy, self.at, self.ox
    local opv, ovx, ovy, oat, oox = self.opv, self.ovx, self.ovy, self.oat, self.oox

    -- 1. подъёмная сила и 2. перепад давления
    for y = 1, chh - 2 do
        local row = y * cw
        for x = 1, cw - 2 do
            local i = row + x
            vx[i] = vx[i] + TSTEPP * (pv[i - 1] - pv[i + 1])
            vy[i] = vy[i] + TSTEPP * (pv[i - cw] - pv[i + cw])
            -- тёплый воздух легче и идёт вверх; холодный опускается
            vy[i] = vy[i] - BUOY * (at[i] - ROOM)
        end
    end

    -- 3-4. перенос потоком: скорость, температура, кислород
    ffi.copy(ovx, vx, n * 8)
    ffi.copy(ovy, vy, n * 8)
    ffi.copy(oat, at, n * 8)
    ffi.copy(oox, ox, n * 8)
    ffi.copy(opv, pv, n * 8)
    for y = 1, chh - 2 do
        local row = y * cw
        for x = 1, cw - 2 do
            local i = row + x
            -- скорость храним в клетках мира, а ячейка воздуха вчетверо
            -- крупнее, поэтому при обратной трассировке делим на CELL
            local tx = x - vx[i] * ADV / CELL
            local ty = y - vy[i] * ADV / CELL
            if tx < 0.5 then tx = 0.5 elseif tx > cw - 1.5 then tx = cw - 1.5 end
            if ty < 0.5 then ty = 0.5 elseif ty > chh - 1.5 then ty = chh - 1.5 end
            local x0, y0 = floor(tx), floor(ty)
            local fx, fy = tx - x0, ty - y0
            local a = y0 * cw + x0
            local b = a + 1
            local c = a + cw
            local d = c + 1
            local w00 = (1 - fx) * (1 - fy)
            local w10 = fx * (1 - fy)
            local w01 = (1 - fx) * fy
            local w11 = fx * fy
            vx[i] = vx[i] * (1 - ADV) + ADV *
                (ovx[a] * w00 + ovx[b] * w10 + ovx[c] * w01 + ovx[d] * w11)
            vy[i] = vy[i] * (1 - ADV) + ADV *
                (ovy[a] * w00 + ovy[b] * w10 + ovy[c] * w01 + ovy[d] * w11)

            -- Давление тоже сносит потоком. Без этого у волны нет
            -- инерции: взрыв раздувается ровным кругом и гаснет на
            -- месте, вместо того чтобы уйти по ветру и закрутиться.
            pv[i] = pv[i] * (1 - PADV) + PADV *
                (opv[a] * w00 + opv[b] * w10 + opv[c] * w01 + opv[d] * w11)

            -- примеси сносит сильнее: они не сопротивляются потоку
            local sx = x - vx[i] * SADV / CELL
            local sy = y - vy[i] * SADV / CELL
            if sx < 0.5 then sx = 0.5 elseif sx > cw - 1.5 then sx = cw - 1.5 end
            if sy < 0.5 then sy = 0.5 elseif sy > chh - 1.5 then sy = chh - 1.5 end
            x0, y0 = floor(sx), floor(sy)
            fx, fy = sx - x0, sy - y0
            a = y0 * cw + x0
            b = a + 1
            c = a + cw
            d = c + 1
            w00 = (1 - fx) * (1 - fy)
            w10 = fx * (1 - fy)
            w01 = (1 - fx) * fy
            w11 = fx * fy
            -- Выборку взвешиваем по свободному объёму: вытянуть воздух
            -- из каменной стены нельзя. Без этого запаянная банка сосёт
            -- кислород прямо из собственных стенок.
            local wl = self.wall
            local f00 = w00 * (1 - wl[a])
            local f10 = w10 * (1 - wl[b])
            local f01 = w01 * (1 - wl[c])
            local f11 = w11 * (1 - wl[d])
            local fs = f00 + f10 + f01 + f11
            if fs > 0.001 then
                local inv = 1 / fs
                at[i] = (oat[a]*f00 + oat[b]*f10 + oat[c]*f01 + oat[d]*f11) * inv
                ox[i] = (oox[a]*f00 + oox[b]*f10 + oox[c]*f01 + oox[d]*f11) * inv
            end
        end
    end

    -- 5. расхождение потока меняет давление
    for y = 1, chh - 2 do
        local row = y * cw
        for x = 1, cw - 2 do
            local i = row + x
            local dp = (vx[i - 1] - vx[i + 1]) + (vy[i - cw] - vy[i + cw])
            local v = pv[i] + TSTEPV * dp
            if v > PMAX then v = PMAX elseif v < -PMAX then v = -PMAX end
            pv[i] = v
        end
    end

    -- 6. размытие давления, тепла и кислорода.
    -- Перенос между соседями ослаблен настолько, насколько они забиты
    -- веществом: через сплошную стену ничего не проходит.
    local wall = self.wall
    ffi.copy(opv, pv, n * 8)
    ffi.copy(oat, at, n * 8)
    ffi.copy(oox, ox, n * 8)
    for y = 1, chh - 2 do
        local row = y * cw
        for x = 1, cw - 2 do
            local i = row + x
            local free = 1 - wall[i]
            local kL = (1 - wall[i - 1]) * free
            local kR = (1 - wall[i + 1]) * free
            local kU = (1 - wall[i - cw]) * free
            local kD = (1 - wall[i + cw]) * free
            -- Углы берём с половинным весом: они дальше по диагонали.
            -- Размытие только по четырём соседям оставляет на поле
            -- давления квадратную сетку — круглая волна идёт ромбом.
            local kA = (1 - wall[i - cw - 1]) * free * 0.5
            local kB = (1 - wall[i - cw + 1]) * free * 0.5
            local kC = (1 - wall[i + cw - 1]) * free * 0.5
            local kE = (1 - wall[i + cw + 1]) * free * 0.5
            local sw = kL + kR + kU + kD + kA + kB + kC + kE
            if sw > 0.001 then
                -- Замыкание здесь заводить нельзя: это была бы одна
                -- таблица на клетку и отказ JIT, поэтому всё вручную.
                local f = sw / 6
                if f > 1 then f = 1 end
                local inv = 1 / sw
                local il, ir = i - 1, i + 1
                local iu, id = i - cw, i + cw
                local ia, ib = iu - 1, iu + 1
                local ic, ie = id - 1, id + 1

                local avg = (kL * opv[il] + kR * opv[ir] + kU * opv[iu] + kD * opv[id]
                          + kA * opv[ia] + kB * opv[ib] + kC * opv[ic] + kE * opv[ie]) * inv
                pv[i] = opv[i] + 0.40 * f * (avg - opv[i])

                avg = (kL * oat[il] + kR * oat[ir] + kU * oat[iu] + kD * oat[id]
                    + kA * oat[ia] + kB * oat[ib] + kC * oat[ic] + kE * oat[ie]) * inv
                at[i] = oat[i] + TDIFF * f * (avg - oat[i])

                avg = (kL * oox[il] + kR * oox[ir] + kU * oox[iu] + kD * oox[id]
                    + kA * oox[ia] + kB * oox[ib] + kC * oox[ic] + kE * oox[ie]) * inv
                ox[i] = oox[i] + ODIFF * f * (avg - oox[i])
            end
        end
    end

    -- 7. потери, плотность воздуха и края мира
    local adens, AIRD0 = self.adens, 12
    for i = 0, n - 1 do
        adens[i] = AIRD0 * 295 / (at[i] + 273)
        pv[i] = pv[i] * PLOSS
        vx[i] = vx[i] * VLOSS
        vy[i] = vy[i] * VLOSS
        at[i] = at[i] + (ROOM - at[i]) * TRELAX
        local o = ox[i]
        if o < 0 then ox[i] = 0 elseif o > 1 then ox[i] = 1 end
    end
    -- на краях мира — обычный воздух: свежий, комнатный, неподвижный
    for x = 0, cw - 1 do
        local b = (chh - 1) * cw + x
        vx[x], vy[x], pv[x], at[x], ox[x] = 0, 0, 0, ROOM, 1
        vx[b], vy[b], pv[b], at[b], ox[b] = 0, 0, 0, ROOM, 1
    end
    for y = 0, chh - 1 do
        local l = y * cw
        local r = l + cw - 1
        vx[l], vy[l], pv[l], at[l], ox[l] = 0, 0, 0, ROOM, 1
        vx[r], vy[r], pv[r], at[r], ox[r] = 0, 0, 0, ROOM, 1
    end
end

Air.CELL = CELL
Air.ROOM = ROOM
return Air
