-- render.lua — вывод мира частиц.
-- Фон кладём одним копированием памяти, затем рисуем только живые частицы.
-- Пустой мир стоит ровно одно копирование.

local ffi = require("ffi")
local bit = require("bit")
local E = require("data.elements")

local render = {}

local BG = {18, 18, 22}
local SHADES = 32

local imageData, image, ptr
local palette, bgBuf, heatBgBuf
local W, H, N

render.mode = "mat"
render.msFill, render.msUpload = 0, 0

----------------------------------------------------------------------
local function buildPalette()
    palette = ffi.new("uint8_t[?]", E.count * SHADES * 3)
    for id = 0, E.count - 1 do
        local e = E.byId[id]
        local cr, cg, cb = BG[1], BG[2], BG[3]
        local amp = 0
        if e and id ~= 0 then
            cr, cg, cb = e.color[1], e.color[2], e.color[3]
            amp = e.shade or 0
        end
        for s = 0, SHADES - 1 do
            local k = (s / (SHADES - 1) - 0.5) * 2 * amp
            local o = (id * SHADES + s) * 3
            local r = cr + k; if r < 0 then r = 0 elseif r > 255 then r = 255 end
            local g = cg + k; if g < 0 then g = 0 elseif g > 255 then g = 255 end
            local b = cb + k; if b < 0 then b = 0 elseif b > 255 then b = 255 end
            palette[o], palette[o + 1], palette[o + 2] = r, g, b
        end
    end
end

local HEAT_STOPS = {
    { -273, { 10,  10,  60} },
    {    0, { 30,  60, 180} },
    {   22, { 40, 110, 200} },
    {  100, { 40, 190, 190} },
    {  300, { 80, 220,  80} },
    {  700, {240, 220,  60} },
    { 1200, {250, 140,  30} },
    { 2000, {255,  80,  40} },
    { 3000, {255, 255, 255} },
}
local PRES_STOPS = {
    { -60, {  20,  40, 200} },
    { -12, {  30,  90, 170} },
    {   0, {  16,  16,  20} },
    {  12, { 170,  90,  30} },
    {  60, { 240, 200,  80} },
    { 200, { 255, 255, 255} },
}
local PRES_N = 512
local PRES_LO, PRES_HI = -60, 200
local presPal

local OXY_STOPS = {
    { 0.00, { 90,  20,  20} },
    { 0.12, {150,  60,  20} },
    { 0.30, {170, 150,  40} },
    { 0.70, { 60, 120, 110} },
    { 1.00, { 30,  60, 120} },
}
local OXY_N = 256
local oxyPal

local HEAT_N = 512
local HEAT_LO, HEAT_HI = -273, 3000
local heatPal

local function heatColor(t)
    local a, b = HEAT_STOPS[1], HEAT_STOPS[#HEAT_STOPS]
    for s = 1, #HEAT_STOPS - 1 do
        if t >= HEAT_STOPS[s][1] and t <= HEAT_STOPS[s + 1][1] then
            a, b = HEAT_STOPS[s], HEAT_STOPS[s + 1]
            break
        end
    end
    local span = b[1] - a[1]
    local f = span > 0 and (t - a[1]) / span or 0
    return a[2][1] + (b[2][1] - a[2][1]) * f,
           a[2][2] + (b[2][2] - a[2][2]) * f,
           a[2][3] + (b[2][3] - a[2][3]) * f
end

local function buildHeatPalette()
    heatPal = ffi.new("uint8_t[?]", HEAT_N * 3)
    for k = 0, HEAT_N - 1 do
        local t = HEAT_LO + (HEAT_HI - HEAT_LO) * k / (HEAT_N - 1)
        local r, g, b = heatColor(t)
        local o = k * 3
        heatPal[o], heatPal[o + 1], heatPal[o + 2] = r, g, b
    end
end

----------------------------------------------------------------------
local function rampColor(stops, t)
    local a, b = stops[1], stops[#stops]
    for s = 1, #stops - 1 do
        if t >= stops[s][1] and t <= stops[s + 1][1] then
            a, b = stops[s], stops[s + 1]
            break
        end
    end
    local span = b[1] - a[1]
    local f = span > 0 and (t - a[1]) / span or 0
    return a[2][1] + (b[2][1] - a[2][1]) * f,
           a[2][2] + (b[2][2] - a[2][2]) * f,
           a[2][3] + (b[2][3] - a[2][3]) * f
end

local function buildPresPalette()
    presPal = ffi.new("uint8_t[?]", PRES_N * 3)
    for k = 0, PRES_N - 1 do
        local t = PRES_LO + (PRES_HI - PRES_LO) * k / (PRES_N - 1)
        local r, g, b = rampColor(PRES_STOPS, t)
        local o = k * 3
        presPal[o], presPal[o + 1], presPal[o + 2] = r, g, b
    end
end

function render.load(w, h)
    W, H, N = w, h, w * h
    imageData = love.image.newImageData(w, h)
    ptr = ffi.cast("uint8_t*", imageData:getFFIPointer())
    image = love.graphics.newImage(imageData)
    image:setFilter("nearest", "nearest")
    buildPalette()
    buildHeatPalette()
    buildPresPalette()
    oxyPal = ffi.new("uint8_t[?]", OXY_N * 3)
    for k = 0, OXY_N - 1 do
        local r, g, b = rampColor(OXY_STOPS, k / (OXY_N - 1))
        local o = k * 3
        oxyPal[o], oxyPal[o + 1], oxyPal[o + 2] = r, g, b
    end

    -- заготовки фона: одно копирование вместо прохода по всем клеткам
    bgBuf = ffi.new("uint8_t[?]", N * 4)
    for i = 0, N - 1 do
        local o = i * 4
        bgBuf[o], bgBuf[o + 1], bgBuf[o + 2], bgBuf[o + 3] = BG[1], BG[2], BG[3], 255
    end
    heatBgBuf = ffi.new("uint8_t[?]", N * 4)
    local r, g, b = heatColor(E.ROOM_TEMP)
    for i = 0, N - 1 do
        local o = i * 4
        heatBgBuf[o], heatBgBuf[o + 1], heatBgBuf[o + 2], heatBgBuf[o + 3] = r, g, b, 255
    end

    render.w, render.h = w, h
end

function render.update(sim)
    local tA = love.timer.getTime()
    local p = ptr
    local mu = sim.maxUsed
    local alive, ptype, px, py = sim.alive, sim.ptype, sim.px, sim.py
    local floor = math.floor
    local w = W

    if render.mode == "oxy" then
        -- Карта кислорода: красное — задохнулось, синее — свежий воздух.
        local air = sim.air
        local aox, acw = air.ox, air.cw
        local pal = oxyPal
        local rshift = require("bit").rshift
        local o = 0
        for yy = 0, H - 1 do
            local arow = rshift(yy, 2) * acw
            for xx = 0, W - 1 do
                local k = aox[arow + rshift(xx, 2)] * (OXY_N - 1)
                if k < 0 then k = 0 elseif k > OXY_N - 1 then k = OXY_N - 1 end
                local q = floor(k) * 3
                p[o] = pal[q]; p[o + 1] = pal[q + 1]; p[o + 2] = pal[q + 2]; p[o + 3] = 255
                o = o + 4
            end
        end
        local pal2, pshd = palette, sim.pshd
        for i = 0, mu - 1 do
            if alive[i] == 1 then
                local k = (ptype[i] * SHADES + bit.rshift(pshd[i], 3)) * 3
                local oo = (floor(py[i]) * w + floor(px[i])) * 4
                p[oo]     = (p[oo] + pal2[k]) * 0.5
                p[oo + 1] = (p[oo + 1] + pal2[k + 1]) * 0.5
                p[oo + 2] = (p[oo + 2] + pal2[k + 2]) * 0.5
            end
        end
    elseif render.mode == "pres" then
        -- Поле давления: единственный режим, где приходится красить каждый
        -- пиксель, потому что давление есть и там, где нет вещества.
        local air = sim.air
        local pv, acw = air.pv, air.cw
        local pal = presPal
        local scale = (PRES_N - 1) / (PRES_HI - PRES_LO)
        local rshift = require("bit").rshift
        local o = 0
        for yy = 0, H - 1 do
            local arow = rshift(yy, 2) * acw
            for xx = 0, W - 1 do
                local k = (pv[arow + rshift(xx, 2)] - PRES_LO) * scale
                if k < 0 then k = 0 elseif k > PRES_N - 1 then k = PRES_N - 1 end
                local q = floor(k) * 3
                p[o] = pal[q]; p[o + 1] = pal[q + 1]; p[o + 2] = pal[q + 2]; p[o + 3] = 255
                o = o + 4
            end
        end
        -- поверх давления обозначим вещество точками
        local pal2, pshd = palette, sim.pshd
        for i = 0, mu - 1 do
            if alive[i] == 1 then
                local k = (ptype[i] * SHADES + bit.rshift(pshd[i], 3)) * 3
                local oo = (floor(py[i]) * w + floor(px[i])) * 4
                p[oo]     = (p[oo] + pal2[k]) * 0.5
                p[oo + 1] = (p[oo + 1] + pal2[k + 1]) * 0.5
                p[oo + 2] = (p[oo + 2] + pal2[k + 2]) * 0.5
            end
        end
    elseif render.mode == "heat" then
        ffi.copy(p, heatBgBuf, N * 4)
        local T = sim.ptmp
        local pal = heatPal
        local scale = (HEAT_N - 1) / (HEAT_HI - HEAT_LO)
        for i = 0, mu - 1 do
            if alive[i] == 1 then
                local k = (T[i] - HEAT_LO) * scale
                if k < 0 then k = 0 elseif k > HEAT_N - 1 then k = HEAT_N - 1 end
                local q = floor(k) * 3
                local o = (floor(py[i]) * w + floor(px[i])) * 4
                p[o] = pal[q]; p[o + 1] = pal[q + 1]; p[o + 2] = pal[q + 2]
            end
        end
    else
        ffi.copy(p, bgBuf, N * 4)
        local pal, pshd = palette, sim.pshd
        for i = 0, mu - 1 do
            if alive[i] == 1 then
                local k = (ptype[i] * SHADES + bit.rshift(pshd[i], 3)) * 3
                local o = (floor(py[i]) * w + floor(px[i])) * 4
                p[o] = pal[k]; p[o + 1] = pal[k + 1]; p[o + 2] = pal[k + 2]
            end
        end
    end

    local tB = love.timer.getTime()
    image:replacePixels(imageData)
    local tC = love.timer.getTime()
    render.msFill = (tB - tA) * 1000
    render.msUpload = (tC - tB) * 1000
end

function render.draw(x, y, scale)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.draw(image, x, y, 0, scale, scale)
end

-- Разметка давления поверх картинки. Поле воздуха живёт на своей сетке
-- (одна ячейка на 4 клетки мира), поэтому стрелки рисуем по ней, прореживая
-- так, чтобы между ними на экране оставалось хотя бы 18 точек.
-- Стрелка показывает, КУДА дует: от сжатого воздуха к разреженному.
-- Знак в ячейке: + перегрузка, − разрежение.
function render.drawPressure(sim, x, y, scale)
    local air = sim.air
    local cw, chh = air.cw, air.chh
    local pv, vx, vy = air.pv, air.vx, air.vy
    local cell = 4 * scale                      -- размер ячейки воздуха на экране
    local stepc = 1
    while cell * stepc < 18 do stepc = stepc + 1 end
    local half = cell * stepc * 0.5

    local g = love.graphics
    for cy = 0, chh - 1, stepc do
        for cx = 0, cw - 1, stepc do
            local i = cy * cw + cx
            local p = pv[i]
            local ux, uy = vx[i], vy[i]
            local sp = math.sqrt(ux * ux + uy * uy)

            local px = x + (cx * 4 * scale) + half
            local py = y + (cy * 4 * scale) + half

            if sp > 0.015 then
                -- длина стрелки растёт со скоростью ветра, но не длиннее ячейки
                local k = sp * 24
                if k > 1 then k = 1 end
                local len = half * 0.9 * k
                local nx, ny = ux / sp, uy / sp
                local ex, ey = px + nx * len, py + ny * len
                if p > 0 then g.setColor(1, 0.45, 0.25, 0.85)
                else g.setColor(0.35, 0.7, 1, 0.85) end
                g.line(px - nx * len, py - ny * len, ex, ey)
                -- наконечник
                local wx, wy = -ny * len * 0.35, nx * len * 0.35
                g.line(ex, ey, ex - nx * len * 0.4 + wx, ey - ny * len * 0.4 + wy)
                g.line(ex, ey, ex - nx * len * 0.4 - wx, ey - ny * len * 0.4 - wy)
            end

            -- Знак перепада там, где давление заметное, а ветер ещё не разогнался:
            -- так виден сам очаг, а не только его последствия.
            local ap = p < 0 and -p or p
            if ap > 0.6 then
                local r = half * 0.3
                if p > 0 then g.setColor(1, 0.3, 0.2, 0.9)
                else g.setColor(0.3, 0.75, 1, 0.9) end
                g.line(px - r, py, px + r, py)
                if p > 0 then g.line(px, py - r, px, py + r) end
            end
        end
    end
    g.setColor(1, 1, 1, 1)
end

return render
