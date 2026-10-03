-- ULTRA POWDER TOY — 2D-песочница веществ.
-- Шаг 5: температура воздуха и конвекция, кислород и удушье, гидростатика
-- со струями и сообщающимися сосудами, вязкость, поверхностное натяжение,
-- плавучий лёд, растворение соли.
-- Разработка: krillxz2362. Особая благодарность AYDIK за помощь в разработке.

local Sim    = require("fiz.sim")
local render = require("ui.render")
local font   = require("ui.font")
local ui     = require("ui.ui")

local VER = "1.2.1"

local state = {
    material = 1,
    brush = 4,
    paused = false,
    stepOnce = false,
    view = "mat",
    arrows = false,
    chemOpen = false,
    chemTab = "tab",
    formulas = false,
    probeT = nil,
    probeP = nil,
    probeO = nil,
}

local sim
local cell, simW, simH
local acc = 0
local STEP = 1 / 60
local fps, fpsAcc, fpsFrames = 0, 0, 0
local msSim, msDraw = 0, 0
local msHeat, msReact, msMove, msAir = 0, 0, 0, 0
local pointers = {}

-- Работает ли на этом устройстве JIT-компилятор. Без него обращения
-- через FFI идут во много раз медленнее, и это надо видеть.
-- ВАЖНО: плеер LOVE на Android стартует с ВЫКЛЮЧЕННЫМ компилятором.
-- Замер на Dimensity 7300: цикл по FFI-частицам 123.2 мс без JIT против
-- 2.1 мс с ним — разница в 59 раз. Включаем принудительно и сами.
local jitInfo = "JIT ?"
do
    local ok, j = pcall(require, "jit")
    if ok and j then
        local was = j.status()
        if not was then pcall(function() j.on(nil, true) end) end
        local now = j.status()
        if now then
            jitInfo = was and "JIT ВКЛ" or "JIT ВКЛ!"   -- "!" = включили сами
        else
            jitInfo = "JIT ВЫКЛ"
        end
    else
        jitInfo = "БЕЗ LUAJIT"
    end
end

local function setupWorld(keep)
    local W, H = love.graphics.getDimensions()
    ui.layout(W, H)

    local area = W * ui.simH
    cell = math.floor(math.sqrt(area / 60000) + 0.5)
    if cell < 2 then cell = 2 end
    if cell > 12 then cell = 12 end

    local nw = math.floor(W / cell)
    local nh = math.floor(ui.simH / cell)
    -- Страховка: если панель почему-то заняла весь экран, мир всё равно
    -- обязан быть хотя бы в одну клетку, иначе LOVE падает на создании
    -- картинки нулевого размера. Так уже случилось, когда ряды кнопок
    -- посчитались по всем 194 веществам.
    if nw < 1 then nw = 1 end
    if nh < 1 then nh = 1 end
    local old = keep and sim or nil

    simW, simH = nw, nh
    sim = Sim.new(simW, simH)
    render.load(simW, simH)

    if old then
        local cw = math.min(old.w, simW)
        local chh = math.min(old.h, simH)
        for y = 0, chh - 1 do
            for x = 0, cw - 1 do
                local occ = old.pmap[y * old.w + x]
                if occ ~= 0 then
                    local i = occ - 1
                    local k = sim:create(x, y, old.ptype[i], old.pshd[i])
                    if k >= 0 then sim.ptmp[k] = old.ptmp[i] end
                end
            end
        end
    end
end

function love.load()
    love.graphics.setDefaultFilter("nearest", "nearest")
    love.graphics.setBackgroundColor(0.07, 0.07, 0.09)
    math.randomseed(os.time())
    font.load()
    setupWorld(false)
end

function love.resize()
    setupWorld(true)
end

----------------------------------------------------------------------
local function paintAt(pxs, pys)
    local gx = math.floor(pxs / cell)
    local gy = math.floor(pys / cell)
    local r = state.brush
    local id = state.material
    local r2 = r * r
    for dy = -r, r do
        for dx = -r, r do
            if dx * dx + dy * dy <= r2 then
                local x, y = gx + dx, gy + dy
                if x >= 0 and y >= 0 and x < simW and y < simH then
                    if id == 0 then
                        sim:killAt(x, y)
                    else
                        sim:create(x, y, id, math.random(0, 255))
                    end
                end
            end
        end
    end
end

local function probe(pxs, pys)
    local gx = math.floor(pxs / cell)
    local gy = math.floor(pys / cell)
    if gx >= 0 and gy >= 0 and gx < simW and gy < simH then
        state.probeT = sim:tempAt(gx, gy)
        state.probeP = sim:pressureAt(gx, gy)
        state.probeO = sim:oxygenAt(gx, gy)
    end
end

local function paintLine(x0, y0, x1, y1)
    local dx, dy = x1 - x0, y1 - y0
    local dist = math.sqrt(dx * dx + dy * dy)
    local steps = math.max(1, math.floor(dist / (cell * 0.5)))
    for i = 0, steps do
        paintAt(x0 + dx * i / steps, y0 + dy * i / steps)
    end
end

local function doAction(b)
    if b.kind == "mat" then
        state.material = b.id
    elseif b.action == "pause" then
        state.paused = not state.paused
    elseif b.action == "step" then
        state.stepOnce = true
    elseif b.action == "brushup" then
        state.brush = math.min(40, state.brush + 2)
    elseif b.action == "brushdown" then
        state.brush = math.max(1, state.brush - 2)
    elseif b.action == "view" then
        state.view = (state.view == "mat") and "heat"
              or (state.view == "heat") and "pres"
              or (state.view == "pres") and "oxy" or "mat"
        render.mode = state.view
    elseif b.action == "arrows" then
        -- Стрелки давления поверх любой карты, не только в режиме «ДАВЛ».
        state.arrows = not state.arrows
    elseif b.action == "chem" then
        state.chemOpen = not state.chemOpen
    elseif b.action == "clear" then
        sim:clear()
    end
end

local function pressed(id, x, y)
    -- Пока открыта таблица элементов, она забирает все нажатия.
    if state.chemOpen then
        local what, pick = ui.chemHit(x, y, state)
        if what == "close" then state.chemOpen = false
        elseif what == "toggle" then state.formulas = not state.formulas
        elseif what == "tab" then state.chemTab = pick
        elseif what == "pick" and pick then state.material = pick end
        return
    end
    local b = ui.hit(x, y)
    if b then
        if b.kind ~= "none" then doAction(b) end
        return
    end
    pointers[id] = { x = x, y = y }
    probe(x, y)
    paintAt(x, y)
end

local function moved(id, x, y)
    if state.chemOpen then return end
    local p = pointers[id]
    if not p then return end
    if y >= ui.y0 then p.x, p.y = x, y; return end
    paintLine(p.x, p.y, x, y)
    probe(x, y)
    p.x, p.y = x, y
end

local function released(id) pointers[id] = nil end

function love.touchpressed(id, x, y)  pressed(id, x, y) end
function love.touchmoved(id, x, y)    moved(id, x, y) end
function love.touchreleased(id)       released(id) end

function love.mousepressed(x, y)
    if love.touch and #love.touch.getTouches() > 0 then return end
    pressed("mouse", x, y)
end
function love.mousemoved(x, y)
    if love.mouse.isDown(1) then moved("mouse", x, y) end
end
function love.mousereleased() released("mouse") end

function love.keypressed(k)
    if k == "escape" then love.event.quit() end
    if k == "space" then state.paused = not state.paused end
    if k == "c" then sim:clear() end
end

----------------------------------------------------------------------
function love.update(dt)
    fpsAcc = fpsAcc + dt
    fpsFrames = fpsFrames + 1
    if fpsAcc >= 0.5 then
        fps = fpsFrames / fpsAcc
        fpsAcc, fpsFrames = 0, 0
    end

    local now = love.timer.getTime

    if state.paused then
        if state.stepOnce then
            state.stepOnce = false
            sim:liquidPressure()
            sim:liquidPressure()
        local a = now(); sim:heat()
            local b = now(); sim:react()
            local c = now(); sim:densities(); sim:couple(); sim:clampWall(); sim.air:update()
            local d = now(); sim:move()
            local e = now()
            msHeat, msReact = (b - a) * 1000, (c - b) * 1000
            msAir, msMove = (d - c) * 1000, (e - d) * 1000
            msSim = msHeat + msReact + msAir + msMove
        end
        return
    end

    acc = acc + dt
    local steps = 0
    local hA, rA, aA, mA = 0, 0, 0, 0
    while acc >= STEP and steps < 2 do
        sim:liquidPressure()
            sim:liquidPressure()
        local a = now(); sim:heat()
        local b = now(); sim:react()
        local c = now(); sim:densities(); sim:couple(); sim:clampWall(); sim.air:update()
        local d = now(); sim:move()
        local e = now()
        hA = hA + (b - a); rA = rA + (c - b)
        aA = aA + (d - c); mA = mA + (e - d)
        acc = acc - STEP
        steps = steps + 1
    end
    if steps > 0 then
        msHeat = hA * 1000 / steps
        msReact = rA * 1000 / steps
        msAir = aA * 1000 / steps
        msMove = mA * 1000 / steps
        msSim = msHeat + msReact + msAir + msMove
    end
    if acc > 0.25 then acc = 0 end
end

function love.draw()
    local t0 = love.timer.getTime()
    render.update(sim)
    render.draw(0, 0, cell)
    -- В режиме «ДАВЛ» поверх карты показываем, куда и насколько гонит воздух.
    if state.arrows or state.view == "pres" then
        render.drawPressure(sim, 0, 0, cell)
    end
    msDraw = (love.timer.getTime() - t0) * 1000

    ui.draw(state)

    love.graphics.setColor(0, 0, 0, 0.45)
    love.graphics.rectangle("fill", 0, 0, ui.W, 42)

    love.graphics.setColor(0.85, 0.9, 1, 1)
    font.print(string.format("ULTRA POWDER TOY %s   %d FPS   ЧАСТИЦ %d",
        VER, math.floor(fps + 0.5), sim.count), 4, 6, 2)

    love.graphics.setColor(1, 0.85, 0.5, 1)
    local awake = 0
    for c = 0, sim.nchunks - 1 do if sim.awake[c] ~= 0 then awake = awake + 1 end end
    font.print(string.format(
        "%s %dx%d ТЕПЛО %.1f РЕАКЦ %.1f ВОЗДУХ %.1f ДВИЖ %.1f БУФЕР %.1f ЗАГР %.1f КУСКОВ %d/%d%s",
        jitInfo, simW, simH, msHeat, msReact, msAir, msMove,
        render.msFill, render.msUpload, awake, sim.nchunks,
        state.probeT and string.format("  %d°  %.0fД  O2 %.2f",
            state.probeT, state.probeP or 0, state.probeO or 1) or ""),
        3, 27, 1)

    -- Таблица элементов рисуется поверх всего остального.
    if state.chemOpen then ui.chemDraw(state) end
end
