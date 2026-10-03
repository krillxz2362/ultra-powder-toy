-- ui.lua — нижняя панель: выбор вещества и управление.

local E = require("data.elements")
local font = require("ui.font")

local ui = {}

ui.cols = 7
ui.rowH = 44
ui.ctrlH = 42

local function fitScale(text, boxW, maxScale)
    local wOne = font.width(text, 1)
    if wOne <= 0 then return 1 end
    local s = math.floor((boxW - 6) / wOne)
    if s < 1 then s = 1 end
    if s > (maxScale or 3) then s = maxScale or 3 end
    return s
end

-- Сколько веществ показывать внизу: всё до первого химического элемента.
-- Элементы (их 118) выбираются в таблице по кнопке ХИМ.
function ui.favCount()
    local n = 0
    for idx = 1, E.count do
        if E.list[idx].z then break end
        n = idx
    end
    return n
end

function ui.layout(W, H)
    -- Рядов столько, сколько нужно избранному плюс ячейка выбранного
    -- элемента. Считать по всем веществам нельзя: с элементами их 194,
    -- панель вышла бы в 28 рядов и выдавила мир с экрана.
    local rows = math.ceil((ui.favCount() + 1) / ui.cols)
    ui.rowH = math.floor(H * 0.052)
    if ui.rowH < 30 then ui.rowH = 30 elseif ui.rowH > 46 then ui.rowH = 46 end
    ui.ctrlH = ui.rowH - 2
    ui.panelH = ui.ctrlH + rows * ui.rowH
    ui.W, ui.H = W, H
    ui.y0 = H - ui.panelH
    ui.chemCells = nil
    ui.colW = W / ui.cols
    ui.rows = rows

    -- кнопки вещества
    -- Внизу — только избранное: прежние вещества. Элементов 118, они
    -- выбираются в таблице по кнопке ХИМ, а последняя ячейка показывает
    -- тот элемент, который сейчас выбран там.
    ui.buttons = {}
    local favN = ui.favCount()
    for idx = 1, favN do
        local e = E.list[idx]
        local c = (idx - 1) % ui.cols
        local r = math.floor((idx - 1) / ui.cols)
        ui.buttons[#ui.buttons + 1] = {
            kind = "mat", id = e.id, name = e.name, color = e.color,
            x = c * ui.colW, y = ui.y0 + ui.ctrlH + r * ui.rowH,
            w = ui.colW, h = ui.rowH,
        }
    end
    do
        local c = favN % ui.cols
        local r = math.floor(favN / ui.cols)
        ui.buttons[#ui.buttons + 1] = {
            kind = "mat", dyn = true, id = -1, name = "ХИМИЯ", color = {60, 70, 90},
            x = c * ui.colW, y = ui.y0 + ui.ctrlH + r * ui.rowH,
            w = ui.colW, h = ui.rowH,
        }
    end

    -- кнопки управления
    local ctrl = { "ПАУЗА", "ШАГ", "<", "КИСТЬ", ">", "ВИД", "ПОТОК", "ХИМ", "ОЧИСТИТЬ" }
    local actions = { "pause", "step", "brushdown", "brush", "brushup", "view", "arrows", "chem", "clear" }
    local wq = { 1.1, 0.7, 0.5, 0.8, 0.5, 0.85, 0.9, 0.8, 1.25 }
    local total = 0
    for _, q in ipairs(wq) do total = total + q end
    local cx = 0
    for i = 1, #ctrl do
        local bw = W * wq[i] / total
        ui.buttons[#ui.buttons + 1] = {
            kind = "ctrl", action = actions[i], name = ctrl[i],
            x = cx, y = ui.y0, w = bw, h = ui.ctrlH,
        }
        cx = cx + bw
    end

    ui.simH = ui.y0
end

function ui.hit(x, y)
    if y < ui.y0 then return nil end
    for _, b in ipairs(ui.buttons) do
        if x >= b.x and x < b.x + b.w and y >= b.y and y < b.y + b.h then
            return b
        end
    end
    return { kind = "none" }
end

local function box(b, fill, border, bw)
    love.graphics.setColor(fill[1] / 255, fill[2] / 255, fill[3] / 255, 1)
    love.graphics.rectangle("fill", b.x + 1, b.y + 1, b.w - 2, b.h - 2)
    love.graphics.setColor(border[1] / 255, border[2] / 255, border[3] / 255, 1)
    love.graphics.setLineWidth(bw or 1)
    love.graphics.rectangle("line", b.x + 1, b.y + 1, b.w - 2, b.h - 2)
end

function ui.draw(state)
    -- подложка панели
    love.graphics.setColor(0.09, 0.09, 0.11, 1)
    love.graphics.rectangle("fill", 0, ui.y0, ui.W, ui.panelH)

    for _, b in ipairs(ui.buttons) do
        if b.kind == "mat" then
            -- Ячейка элемента показывает текущий выбор из таблицы.
            -- Ячейка показывает то, что выбрано на химическом экране:
            -- элемент из таблицы или соединение из списка.
            local e = b.dyn and E.byId[state.material] or nil
            if b.dyn and e and not (e.z or e.cmp) then e = nil end
            local bid = e and e.id or b.id
            local bname = b.name
            if e then
                bname = state.formulas and (e.sym or e.formula or e.name) or e.name
            end
            local sel = (state.material == bid)
            local fill = bid == 0 and {40, 40, 46} or (e and e.color or b.color)
            box(b, fill, sel and {255, 240, 120} or {24, 24, 28}, sel and 3 or 1)

            -- подпись: тёмная на светлом, светлая на тёмном
            local lum = (fill[1] * 0.3 + fill[2] * 0.59 + fill[3] * 0.11)
            if lum > 140 then
                love.graphics.setColor(0.05, 0.05, 0.06, 1)
            else
                love.graphics.setColor(0.95, 0.95, 0.97, 1)
            end
            local s = fitScale(bname, b.w, 3)
            font.printCentered(bname, b.x + b.w / 2,
                b.y + (b.h - font.height(s)) / 2, s)
        else
            local on = (b.action == "pause" and state.paused)
                or (b.action == "view" and state.view ~= "mat")
            if b.action == "arrows" and state.arrows then on = true end
            if b.action == "chem" and state.chemOpen then on = true end
            box(b, on and {170, 60, 50} or {46, 46, 54}, {80, 80, 92}, 1)
            love.graphics.setColor(0.92, 0.92, 0.95, 1)
            local label = b.name
            if b.action == "brush" then label = tostring(state.brush) end
            if b.action == "view" then
                label = (state.view == "heat") and "ТЕПЛО"
                     or (state.view == "pres") and "ДАВЛ"
                     or (state.view == "oxy") and "КИСЛ" or "ВИД"
            end
            if b.action == "pause" and state.paused then label = "ПУСК" end
            local s = fitScale(label, b.w, 2)
            font.printCentered(label, b.x + b.w / 2,
                b.y + (b.h - font.height(s)) / 2, s)
        end
    end
end


----------------------------------------------------------------------
-- Таблица Менделеева: выбор элемента во весь экран.
-- Клетки стоят на своих местах по группам и периодам, лантаноиды
-- и актиноиды — отдельными рядами внизу, как в обычной таблице.
----------------------------------------------------------------------
local chem = require("data.chem")
local RX   = require("data.chemrx")

-- Для каждого номера берём ту форму вещества, в которой элемент
-- бывает при комнатной температуре: ртуть жидкая, хлор газ, железо твёрдое.
ui.idByZ = {}
for id = 0, E.count - 1 do
    local e = E.byId[id]
    if e.z then
        local cur = ui.idByZ[e.z]
        if not cur or (e.temp or 22) == E.ROOM_TEMP then
            if not cur or (E.byId[cur].temp or 22) ~= E.ROOM_TEMP then
                ui.idByZ[e.z] = id
            end
        end
    end
end

-- Список соединений: всё, что пришло из compounds.lua.
ui.cmpList = {}
for id = 0, E.count - 1 do
    local e = E.byId[id]
    if e.cmp then ui.cmpList[#ui.cmpList + 1] = { id = id, f = e.formula, ru = e.name, e = e } end
end

-- Шрифт пиксельный, нижних индексов и стрелок в нём нет: приводим
-- уравнение к виду, который он умеет рисовать.
local SUBS = {
    ["\u{2080}"]="0", ["\u{2081}"]="1", ["\u{2082}"]="2", ["\u{2083}"]="3",
    ["\u{2084}"]="4", ["\u{2085}"]="5", ["\u{2086}"]="6", ["\u{2087}"]="7",
    ["\u{2088}"]="8", ["\u{2089}"]="9",
    ["\u{2192}"]="->", ["\u{21C4}"]="<->", ["\u{2193}"]="v",
}
function ui.plainEq(s)
    -- По одному символу: диапазон многобайтовых знаков в шаблоне Lua
    -- не работает, там байты, а не символы.
    for k, v in pairs(SUBS) do s = s:gsub(k, v) end
    return s
end

ui.chemCells = nil
ui.chemTabs  = nil

----------------------------------------------------------------------
-- Раскладка химического экрана
----------------------------------------------------------------------
local function layoutChem()
    local W, H = love.graphics.getDimensions()
    local pad, topH = 4, 34
    local tabH = 30
    local y0 = topH + pad + tabH + pad

    -- верхний ряд: подписи и закрыть
    ui.chemToggle = { x = 6, y = 4, w = W * 0.42, h = topH - 8 }
    ui.chemClose  = { x = W - W * 0.22 - 6, y = 4, w = W * 0.22, h = topH - 8 }

    -- вкладки
    local names = { { "tab", "ТАБЛИЦА" }, { "cmp", "ВЕЩЕСТВА" }, { "rec", "РЕЦЕПТЫ" } }
    ui.chemTabs = {}
    local tw = (W - pad * 4) / 3
    for i, n in ipairs(names) do
        ui.chemTabs[i] = { key = n[1], label = n[2],
            x = pad + (i - 1) * (tw + pad), y = topH + pad, w = tw, h = tabH }
    end

    -- таблица Менделеева
    local cs = math.min((W - pad * 2) / 18, (H - y0 - pad) / 10)
    local x0 = (W - cs * 18) / 2
    ui.chemCells = {}
    for _, e in ipairs(chem.el) do
        ui.chemCells[#ui.chemCells + 1] = {
            z = e.z, sym = e.sym, ru = e.ru, cat = e.cat,
            r = e.r, g = e.g, b = e.b,
            x = x0 + (e.x - 1) * cs, y = y0 + (e.y - 1) * cs,
            w = cs - 1, h = cs - 1,
        }
    end
    ui.chemCS = cs
    ui.chemBottom = y0 + cs * 10

    -- сетка соединений
    local cols = math.max(3, math.floor(W / 170))
    local rows = math.ceil(#ui.cmpList / cols)
    local bw = (W - pad * (cols + 1)) / cols
    local bh = math.min(52, math.max(30, (H - y0 - pad) / rows))
    ui.cmpCells = {}
    for i, c in ipairs(ui.cmpList) do
        local cx = (i - 1) % cols
        local cy = math.floor((i - 1) / cols)
        ui.cmpCells[i] = { id = c.id, f = c.f, ru = c.ru, e = c.e,
            x = pad + cx * (bw + pad), y = y0 + cy * bh, w = bw, h = bh - 2 }
    end
    ui.cmpTop = y0
    ui.chemTop = y0
end

----------------------------------------------------------------------
-- Отрисовка
----------------------------------------------------------------------
local function drawButton(g, b, label, on, scale)
    g.setColor(on and 0.32 or 0.18, on and 0.36 or 0.20, on and 0.46 or 0.26, 1)
    g.rectangle("fill", b.x, b.y, b.w, b.h)
    g.setColor(on and 0.75 or 0.38, on and 0.82 or 0.42, on and 0.95 or 0.50, 1)
    g.rectangle("line", b.x + 0.5, b.y + 0.5, b.w - 1, b.h - 1)
    g.setColor(1, 1, 1, on and 1 or 0.72)
    local s = scale or 2
    font.print(label, b.x + (b.w - font.width(label, s)) / 2,
        b.y + (b.h - font.height(s)) / 2, s)
end

local function drawTable(g, state)
    local cs = ui.chemCS
    local sc = math.max(1, math.floor(cs / 13))
    for _, c in ipairs(ui.chemCells) do
        local sel = (state.material == ui.idByZ[c.z])
        g.setColor(c.r / 255 * 0.5, c.g / 255 * 0.5, c.b / 255 * 0.5, 1)
        g.rectangle("fill", c.x, c.y, c.w, c.h)
        if sel then
            g.setColor(1, 0.85, 0.3, 1)
            g.rectangle("line", c.x + 0.5, c.y + 0.5, c.w - 1, c.h - 1)
            g.rectangle("line", c.x + 1.5, c.y + 1.5, c.w - 3, c.h - 3)
        end
        g.setColor(1, 1, 1, 0.55)
        font.print(tostring(c.z), c.x + 2, c.y + 1, 1)
        g.setColor(1, 1, 1, 1)
        font.print(c.sym, c.x + (c.w - font.width(c.sym, sc)) / 2,
            c.y + c.h * (cs >= 26 and 0.30 or 0.5) - font.height(sc) * 0.5, sc)
        if cs >= 26 then
            local nm, maxc = c.ru, math.floor((c.w - 2) / 6)
            if #nm > maxc then
                local out, n = {}, 0
                for ch in nm:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
                    n = n + 1
                    if n > maxc then break end
                    out[#out + 1] = ch
                end
                nm = table.concat(out)
            end
            g.setColor(1, 1, 1, 0.75)
            font.print(nm, c.x + (c.w - font.width(nm, 1)) / 2,
                c.y + c.h - font.height(1) - 2, 1)
        end
    end

    -- карточка выбранного элемента
    local e = E.byId[state.material]
    if e and e.z then
        local ce = chem.byZ[e.z]
        local bx, by = 8, ui.chemBottom + 14
        local lh = font.height(2) + 6
        g.setColor(ce.r / 255, ce.g / 255, ce.b / 255, 1)
        font.print(ce.sym, bx, by, 4)
        g.setColor(1, 1, 1, 1)
        font.print(ce.ru, bx + font.width(ce.sym, 4) + 10, by + 4, 3)
        local function line(k, v, n)
            g.setColor(0.55, 0.62, 0.72, 1)
            font.print(k, bx, by + lh * (n + 1.4), 2)
            g.setColor(0.92, 0.95, 1, 1)
            font.print(v, bx + 150, by + lh * (n + 1.4), 2)
        end
        local function temp(t)
            if not t then return "—" end
            return string.format("%d°", math.floor(t + 0.5))
        end
        line("НОМЕР", tostring(ce.z), 0)
        line("МАССА", string.format("%.3f", ce.mass), 1)
        line("ПЛОТНОСТЬ", string.format("%d кг/м3", e.density), 2)
        line("ПЛАВЛЕНИЕ", temp(ce.melt), 3)
        line("КИПЕНИЕ", temp(ce.boil), 4)
        line("ГРУППА", ce.cat, 5)
        line("ЭЛЕКТРООТР", ce.en > 0 and string.format("%.2f", ce.en) or "—", 6)
        line("СЕЙЧАС", e.name, 7)
    end
end

local function drawCompounds(g, state)
    for _, c in ipairs(ui.cmpCells) do
        local sel = (state.material == c.id)
        local col = c.e.color
        g.setColor(col[1] / 255 * 0.45, col[2] / 255 * 0.45, col[3] / 255 * 0.45, 1)
        g.rectangle("fill", c.x, c.y, c.w, c.h)
        if sel then
            g.setColor(1, 0.85, 0.3, 1)
            g.rectangle("line", c.x + 0.5, c.y + 0.5, c.w - 1, c.h - 1)
        end
        g.setColor(1, 1, 1, 1)
        font.print(c.f, c.x + 4, c.y + 3, 2)
        g.setColor(1, 1, 1, 0.70)
        local nm, maxc = c.ru, math.floor((c.w - 6) / 6)
        if #nm > maxc * 2 then nm = nm end
        font.print(nm, c.x + 4, c.y + c.h - font.height(1) - 3, 1)
    end
end

local function drawRecipes(g, state)
    local W = ui.W
    local e = E.byId[state.material]
    local y = ui.chemTop + 4
    g.setColor(1, 1, 1, 1)
    local title = e and (e.formula or e.name) or "—"
    font.print(title, 8, y, 3)
    y = y + font.height(3) + 4
    if e and e.formula then
        g.setColor(0.7, 0.78, 0.9, 1)
        font.print(e.name, 8, y, 2)
        y = y + font.height(2) + 8
    else
        y = y + 8
    end

    local recs = RX.recipes[state.material]
    if not recs or #recs == 0 then
        g.setColor(0.7, 0.72, 0.78, 1)
        font.print("ЭТО ВЕЩЕСТВО НЕ ПОЛУЧИТЬ РЕАКЦИЕЙ.", 8, y, 2)
        font.print("ВЫБЕРИ ДРУГОЕ НА ВКЛАДКЕ ВЕЩЕСТВА.", 8, y + font.height(2) + 6, 2)
        return
    end

    g.setColor(0.6, 0.68, 0.8, 1)
    font.print("КАК ПОЛУЧИТЬ:", 8, y, 2)
    y = y + font.height(2) + 8

    for i, r in ipairs(recs) do
        if y > ui.H - 60 then break end
        g.setColor(0.14, 0.16, 0.20, 1)
        g.rectangle("fill", 6, y - 3, W - 12, font.height(2) * 2 + 14)
        g.setColor(1, 1, 1, 1)
        font.print(ui.plainEq(r.eq), 10, y, 2)
        local cond
        if r.dec then
            cond = string.format("НАГРЕТЬ ДО %d°", math.floor(r.t))
        else
            cond = (r.t and r.t > 25) and string.format("ОТ %d°", math.floor(r.t)) or "ПРИ ЛЮБОЙ ТЕМПЕРАТУРЕ"
            if r.tmax and r.tmax < 9999 then
                cond = cond .. string.format(" ДО %d°", math.floor(r.tmax))
            end
        end
        if r.cat then cond = cond .. ", КАТАЛИЗАТОР " .. r.cat end
        if r.rule then cond = cond .. " (ПО ПРАВИЛУ)" end
        g.setColor(0.62, 0.70, 0.82, 1)
        font.print(cond, 10, y + font.height(2) + 4, 1)
        y = y + font.height(2) * 2 + 18
    end
end

function ui.chemDraw(state)
    if not ui.chemCells then layoutChem() end
    local g = love.graphics
    local W, H = ui.W, ui.H
    g.setColor(0.04, 0.05, 0.07, 0.97)
    g.rectangle("fill", 0, 0, W, H)

    drawButton(g, ui.chemToggle,
        state.formulas and "ПОДПИСИ: ФОРМУЛЫ" or "ПОДПИСИ: НАЗВАНИЯ", false)
    local b = ui.chemClose
    g.setColor(0.35, 0.16, 0.16, 1)
    g.rectangle("fill", b.x, b.y, b.w, b.h)
    g.setColor(0.85, 0.45, 0.45, 1)
    g.rectangle("line", b.x + 0.5, b.y + 0.5, b.w - 1, b.h - 1)
    g.setColor(1, 1, 1, 1)
    font.print("ЗАКРЫТЬ", b.x + (b.w - font.width("ЗАКРЫТЬ", 2)) / 2,
        b.y + (b.h - font.height(2)) / 2, 2)

    local tab = state.chemTab or "tab"
    for _, t in ipairs(ui.chemTabs) do
        drawButton(g, t, t.label, t.key == tab)
    end

    if tab == "cmp" then
        drawCompounds(g, state)
    elseif tab == "rec" then
        drawRecipes(g, state)
    else
        drawTable(g, state)
    end
end

----------------------------------------------------------------------
-- Нажатия
----------------------------------------------------------------------
local function inside(b, x, y)
    return x >= b.x and x < b.x + b.w and y >= b.y and y < b.y + b.h
end

function ui.chemHit(x, y, state)
    if not ui.chemCells then return nil end
    if inside(ui.chemToggle, x, y) then return "toggle" end
    if inside(ui.chemClose, x, y) then return "close" end
    for _, t in ipairs(ui.chemTabs) do
        if inside(t, x, y) then return "tab", t.key end
    end

    local tab = state and state.chemTab or "tab"
    if tab == "cmp" then
        for _, c in ipairs(ui.cmpCells) do
            if inside(c, x, y) then return "pick", c.id end
        end
    elseif tab == "tab" then
        for _, c in ipairs(ui.chemCells) do
            if inside(c, x, y) then return "pick", ui.idByZ[c.z] end
        end
    end
    return nil
end

return ui
