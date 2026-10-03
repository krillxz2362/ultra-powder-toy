-- elements.lua — таблица веществ на настоящих физических величинах.
--
-- Плотность: кг/м³, как в справочнике (вода 1000, железо 7800, ртуть 13600).
-- От неё работает закон Архимеда, поэтому числа должны быть честными.
-- Теплоёмкость: кДж/(кг·К), тоже справочная (вода 4.18, железо 0.45).
-- Скрытая теплота плавления и парообразования задана в "градусном"
-- эквиваленте: сколько градусов перегрева надо накопить, чтобы вещество
-- действительно сменило состояние. Благодаря ей вода стоит на 100° всё
-- время кипения, а не перескакивает в пар мгновенно.

local ffi = require("ffi")

local E = {}

E.EMPTY  = 0
E.POWDER = 1
E.LIQUID = 2
E.GAS    = 3
E.SOLID  = 4

E.AIR_DENSITY = 12        -- воздух при комнатной температуре, кг/м³ ×10
E.ROOM_TEMP   = 22
E.TEMP_MIN    = -273
E.TEMP_MAX    = 3500

-- Поля вещества:
--   density   плотность, кг/м³ (для Архимеда и вытеснения)
--   cap       удельная теплоёмкость
--   cond      теплопроводность 0..1
--   melt/boil/cool  {at=, to=} пороги превращений
--   latF/latV скрытая теплота плавления и парообразования
--   ignite    температура воспламенения
--   flam      вероятность вспыхнуть за шаг
--   residue   что остаётся после горения
--   life      срок жизни, decayTo — во что превращается
--   repose    подвижность сыпучего: 1 растекается как вода, 0.3 стоит горкой
--   advec     насколько частицу несёт ветром
--   airDrag   насколько сама толкает воздух
--   airLoss   насколько гасит воздух в своей клетке (0 — глухая стена)
--   hotAir    давление, которое вещество создаёт само (огонь, пар)
--   expand    прирост давления от нагрева
--   expans    коэффициент теплового расширения, 1/К (справочный)
--   oxyUse    сколько кислорода сжигает за шаг
--   oxyNeed   ниже этой доли кислорода горение невозможно
--   tension   поверхностное натяжение: ртуть собирается в шарики
--   visc      вязкость: лава ползёт, вода растекается
--   blast     сила взрыва при воспламенении
--   shock     взрывается от удара со скоростью выше этой
--   fixed     температура не меняется
--   acidProof кислота не берёт

E.list = {
{id=0,  key="VOID",   name="ЛАСТИК",     state=E.EMPTY,  density=12,    temp=22,   cond=0.03, cap=1.0,  color={0,0,0},       shade=0},

{id=1,  key="SAND",   name="ПЕСОК",      state=E.POWDER, density=1600,  temp=22,   cond=0.08, cap=0.83, expans=2.0e-05, color={214,185,110}, shade=18,
    melt={at=1700, to=14}, latF=140, repose=0.92},

{id=2,  key="WATER",  name="ВОДА",       state=E.LIQUID, density=1000,  temp=22,   cond=0.12, cap=4.18, expans=2.1e-04, tension=0.35, visc=0.03, color={52,110,205},  shade=14,
    boil={at=100, to=11}, cool={at=0, to=13}, latV=2257, latF=334},

{id=3,  key="STONE",  name="КАМЕНЬ",     state=E.SOLID,  density=2700,  temp=22,   cond=0.25, cap=0.85, expans=2.0e-05, color={122,122,128}, shade=16,
    melt={at=1300, to=14}, latF=200},

{id=4,  key="WOOD",   name="ДЕРЕВО",     state=E.SOLID,  density=600,   temp=22,   cond=0.05, cap=1.70, expans=1.5e-05, color={122,82,44},   shade=14,
    ignite=300, flam=0.060, residue=17},

{id=5,  key="METAL",  name="МЕТАЛЛ",     state=E.SOLID,  density=7800,  temp=22,   cond=0.95, cap=0.45, expans=3.5e-05, color={168,172,182}, shade=10,
    melt={at=1500, to=14}, latF=270},

{id=6,  key="GRAVEL", name="ГРАВИЙ",     state=E.POWDER, density=1800,  temp=22,   cond=0.10, cap=0.84, expans=2.0e-05, color={138,130,118}, shade=22,
    melt={at=1300, to=14}, latF=200, repose=0.38},

{id=7,  key="SALT",   name="СОЛЬ",       state=E.POWDER, density=2160,  temp=22,   cond=0.12, cap=0.88, expans=3.0e-05, color={236,238,240}, shade=10,
    melt={at=800, to=14}, latF=520, repose=0.85, active=true},

{id=8,  key="COAL",   name="УГОЛЬ",      state=E.POWDER, density=1300,  temp=22,   cond=0.06, cap=1.26, expans=2.0e-05, color={46,44,48},    shade=12,
    ignite=400, flam=0.015, residue=17, repose=0.55},

{id=9,  key="OIL",    name="МАСЛО",      state=E.LIQUID, density=900,   temp=22,   cond=0.06, cap=2.00, expans=7.0e-04, tension=0.15, visc=0.35, color={74,54,36},    shade=10,
    ignite=150, flam=0.080, boil={at=320, to=12}, latV=250},

{id=10, key="ACID",   name="КИСЛОТА",    state=E.LIQUID, density=1200,  temp=22,   cond=0.12, cap=3.10, expans=3.0e-04, tension=0.20, visc=0.05, color={130,210,60},  shade=14,
    boil={at=200, to=12}, latV=900},

{id=11, key="STEAM",  name="ПАР",        state=E.GAS,    density=6,     temp=120,  cond=0.05, cap=2.00, color={192,200,212}, shade=12,
    cool={at=95, to=2}, latV=2257, hotAir=0.0005, expand=0.0000090},

{id=12, key="GAS",    name="ГАЗ",        state=E.GAS,    density=20,    temp=22,   cond=0.04, cap=1.70, color={150,176,110}, shade=12,
    ignite=120, flam=0.400, blast=0.30, expand=0.0000060},

-- Лёд легче воды (917 против 1000) и обязан плавать. Поэтому он не
-- неподвижное тело, а подвижное: падает, всплывает, складывается глыбами.
-- Подвижность 0 означает, что вбок он не растекается, как настоящая льдина.
{id=13, key="ICE",    name="ЛЁД",        state=E.POWDER, density=917,   temp=-15,  cond=0.25, cap=2.10, expans=1.6e-04, color={150,205,235}, shade=12,
    melt={at=2, to=2}, latF=334, repose=0, drag=0.985},

{id=14, key="LAVA",   name="ЛАВА",       state=E.LIQUID, density=3000,  temp=1500, cond=0.30, cap=2.50, expans=1.0e-04, tension=0.50, visc=0.90, color={225,110,30},  shade=20,
    cool={at=700, to=3}, latF=200, drag=0.80, jitter=0.05, hotAir=0.0003, expand=0.0000020},

{id=15, key="FIRE",   name="ОГОНЬ",      state=E.GAS,    density=4,     temp=900,  cond=0.25, cap=1.00, color={240,150,40},  shade=30,
    life=50, decayTo=16, jitter=0.55, hotAir=0.0016, expand=0.0000120,
    oxyUse=0.020, oxyNeed=0.14},

{id=16, key="SMOKE",  name="ДЫМ",        state=E.GAS,    density=8,     temp=180,  cond=0.04, cap=1.10, color={70,70,76},    shade=14,
    life=170, decayTo=0, jitter=0.35, hotAir=0.0002, expand=0.0000040},

{id=17, key="ASH",    name="ЗОЛА",       state=E.POWDER, density=700,   temp=60,   cond=0.05, cap=0.90, expans=2.0e-05, color={96,92,88},    shade=16,
    repose=0.62},

{id=18, key="GLASS",  name="СТЕКЛО",     state=E.SOLID,  density=2500,  temp=22,   cond=0.20, cap=0.84, expans=2.5e-05, color={176,206,212}, shade=8,
    melt={at=1700, to=14}, latF=140, acidProof=true},

{id=19, key="HEAT",   name="НАГРЕВ",     state=E.SOLID,  density=7800,  temp=1800, cond=1.00, cap=0.45, color={190,40,40},   shade=8,
    fixed=true, acidProof=true},

{id=20, key="COLD",   name="ХОЛОД",      state=E.SOLID,  density=7800,  temp=-150, cond=1.00, cap=0.45, color={60,120,200},  shade=8,
    fixed=true, acidProof=true},

{id=21, key="MERCURY",name="РТУТЬ",      state=E.LIQUID, density=13600, temp=22,   cond=0.45, cap=0.14, expans=1.8e-04, tension=0.88, visc=0.02, color={190,190,200}, shade=10,
    boil={at=357, to=12}, cool={at=-39, to=5}, latV=295, latF=11, drag=0.90},

{id=22, key="GUNPOWDER", name="ПОРОХ",   state=E.POWDER, density=1700,  temp=22,   cond=0.07, cap=1.00, expans=3.0e-05, color={62,60,70},    shade=14,
    ignite=250, flam=0.500, blast=1.2, residue=17, repose=0.80},

{id=23, key="TNT",    name="ТРОТИЛ",     state=E.SOLID,  density=1650,  temp=22,   cond=0.06, cap=1.40, expans=3.0e-05, color={176,74,52},   shade=10,
    ignite=300, flam=0.900, blast=5.0},

{id=24, key="THERMITE", name="ТЕРМИТ",   state=E.POWDER, density=4000,  temp=22,   cond=0.20, cap=0.70, expans=2.0e-05, color={150,96,40},   shade=14,
    ignite=900, flam=0.120, blast=0.4, residue=14, repose=0.60, burnTemp=2600, burnLife=200, burnTo=14},

{id=26, key="BRINE",  name="РАССОЛ",     state=E.LIQUID, density=1030,  temp=22,   cond=0.12, cap=3.90, expans=2.5e-04, tension=0.30, visc=0.04, color={92,140,170}, shade=12,
    boil={at=103, to=11}, cool={at=-8, to=13}, latV=2257, latF=334},

{id=25, key="NITRO",  name="НИТРО",      state=E.LIQUID, density=1600,  temp=22,   cond=0.06, cap=1.50, expans=6.0e-04, tension=0.10, visc=0.08, color={210,200,90},  shade=10,
    ignite=200, flam=0.800, blast=4.0, shock=1.2, drag=0.970},

-- ============================================================
-- Химические элементы. Данные — chem.lua (справочные величины).
-- У ходовых элементов заведены жидкая и газовая формы с настоящими
-- точками плавления и кипения; остальные — в том состоянии, в котором
-- бывают при комнатной температуре. Цвет по схеме CPK.
-- Плотность твёрдой и жидкой фазы у газов в справочнике отсутствует,
-- она оценена по молярному объёму жидкости (~30 см3/моль).
-- Поле temp — температура, при которой вещество появляется: расплав
-- создаётся горячим, иначе он застывал бы в тот же миг.
-- ============================================================
{id=27, key="H", name="Водород", sym="H", z=1, state=E.POWDER, density=60, temp=-270, cond=0.15, cap=15.00, color={255,255,255}, shade=10, repose=0.55,
    melt={at=-259.2, to=28}},
{id=28, key="H_L", name="Водород (ж)", sym="H", z=1, state=E.LIQUID, density=60, temp=-263, cond=0.15, cap=15.00, color={255,255,255}, shade=10, tension=0.45, visc=0.10,
    cool={at=-259.2, to=27}, boil={at=-252.9, to=29}},
{id=29, key="H_G", name="Водород (г)", sym="H", z=1, state=E.GAS, density=2, temp=22, cond=0.15, cap=15.00, color={255,255,255}, shade=10, expand=0.0000060,
    cool={at=-252.9, to=28}},
{id=30, key="HE", name="Гелий", sym="He", z=2, state=E.SOLID, density=144, temp=-270, cond=0.05, cap=0.52, color={217,255,255}, shade=10,
    melt={at=-272.2, to=31}},
{id=31, key="HE_L", name="Гелий (ж)", sym="He", z=2, state=E.LIQUID, density=133, temp=-270, cond=0.05, cap=0.52, color={217,255,255}, shade=10, tension=0.45, visc=0.10,
    cool={at=-272.2, to=30}, boil={at=-268.9, to=32}},
{id=32, key="HE_G", name="Гелий (г)", sym="He", z=2, state=E.GAS, density=2, temp=22, cond=0.05, cap=0.52, color={217,255,255}, shade=10, expand=0.0000060,
    cool={at=-268.9, to=31}},
{id=33, key="LI", name="Литий", sym="Li", z=3, state=E.SOLID, density=577, temp=22, cond=0.85, cap=3.58, color={204,128,255}, shade=10},
{id=34, key="BE", name="Бериллий", sym="Be", z=4, state=E.SOLID, density=1998, temp=22, cond=0.85, cap=1.82, color={194,255,0}, shade=10},
{id=35, key="B", name="Бор", sym="B", z=5, state=E.POWDER, density=2246, temp=22, cond=0.40, cap=1.03, color={255,181,181}, shade=10, repose=0.55},
{id=36, key="C", name="Углерод", sym="C", z=6, state=E.POWDER, density=1967, temp=22, cond=0.15, cap=0.71, color={144,144,144}, shade=10, repose=0.55},
{id=37, key="N", name="Азот", sym="N", z=7, state=E.POWDER, density=504, temp=-240, cond=0.15, cap=0.90, color={48,80,248}, shade=10, repose=0.55,
    melt={at=-210, to=38}},
{id=38, key="N_L", name="Азот (ж)", sym="N", z=7, state=E.LIQUID, density=467, temp=-206, cond=0.15, cap=0.90, color={48,80,248}, shade=10, tension=0.45, visc=0.10,
    cool={at=-210, to=37}, boil={at=-195.8, to=39}},
{id=39, key="N_G", name="Азот (г)", sym="N", z=7, state=E.GAS, density=6, temp=22, cond=0.15, cap=0.90, color={48,80,248}, shade=10, expand=0.0000060,
    cool={at=-195.8, to=38}},
{id=40, key="O", name="Кислород", sym="O", z=8, state=E.POWDER, density=576, temp=-249, cond=0.15, cap=0.90, color={255,13,13}, shade=10, repose=0.55,
    melt={at=-218.8, to=41}},
{id=41, key="O_L", name="Кислород (ж)", sym="O", z=8, state=E.LIQUID, density=533, temp=-199, cond=0.15, cap=0.90, color={255,13,13}, shade=10, tension=0.45, visc=0.10,
    cool={at=-218.8, to=40}, boil={at=-183, to=42}},
{id=42, key="O_G", name="Кислород (г)", sym="O", z=8, state=E.GAS, density=7, temp=22, cond=0.15, cap=0.90, color={255,13,13}, shade=10, expand=0.0000060,
    cool={at=-183, to=41}},
{id=43, key="F_G", name="Фтор (г)", sym="F", z=9, state=E.GAS, density=8, temp=22, cond=0.15, cap=0.90, color={144,224,80}, shade=10, expand=0.0000060},
{id=44, key="NE_G", name="Неон (г)", sym="Ne", z=10, state=E.GAS, density=9, temp=22, cond=0.05, cap=0.52, color={179,227,245}, shade=10, expand=0.0000060},
{id=45, key="NA", name="Натрий", sym="Na", z=11, state=E.SOLID, density=1045, temp=22, cond=0.85, cap=1.23, color={171,92,242}, shade=10,
    melt={at=97.8, to=46}},
{id=46, key="NA_L", name="Натрий (ж)", sym="Na", z=11, state=E.LIQUID, density=968, temp=490, cond=0.85, cap=1.23, color={171,92,242}, shade=10, tension=0.45, visc=0.10,
    cool={at=97.8, to=45}, boil={at=882.9, to=47}},
{id=47, key="NA_G", name="Натрий (г)", sym="Na", z=11, state=E.GAS, density=10, temp=923, cond=0.85, cap=1.23, color={171,92,242}, shade=10, expand=0.0000060,
    cool={at=882.9, to=46}},
{id=48, key="MG", name="Магний", sym="Mg", z=12, state=E.SOLID, density=1877, temp=22, cond=0.85, cap=1.02, color={138,255,0}, shade=10,
    melt={at=649.9, to=49}},
{id=49, key="MG_L", name="Магний (ж)", sym="Mg", z=12, state=E.LIQUID, density=1738, temp=870, cond=0.85, cap=1.02, color={138,255,0}, shade=10, tension=0.45, visc=0.10,
    cool={at=649.9, to=48}, boil={at=1089.8, to=50}},
{id=50, key="MG_G", name="Магний (г)", sym="Mg", z=12, state=E.GAS, density=11, temp=1130, cond=0.85, cap=1.02, color={138,255,0}, shade=10, expand=0.0000060,
    cool={at=1089.8, to=49}},
{id=51, key="AL", name="Алюминий", sym="Al", z=13, state=E.SOLID, density=2916, temp=22, cond=0.80, cap=0.90, color={191,166,166}, shade=10,
    melt={at=660.3, to=52}},
{id=52, key="AL_L", name="Алюминий (ж)", sym="Al", z=13, state=E.LIQUID, density=2700, temp=1565, cond=0.80, cap=0.90, color={191,166,166}, shade=10, tension=0.45, visc=0.10,
    cool={at=660.3, to=51}, boil={at=2469.8, to=53}},
{id=53, key="AL_G", name="Алюминий (г)", sym="Al", z=13, state=E.GAS, density=12, temp=2510, cond=0.80, cap=0.90, color={191,166,166}, shade=10, expand=0.0000060,
    cool={at=2469.8, to=52}},
{id=54, key="SI", name="Кремний", sym="Si", z=14, state=E.POWDER, density=2515, temp=22, cond=0.40, cap=0.70, color={240,200,160}, shade=10, repose=0.55,
    melt={at=1413.8, to=55}},
{id=55, key="SI_L", name="Кремний (ж)", sym="Si", z=14, state=E.LIQUID, density=2329, temp=2339, cond=0.40, cap=0.70, color={240,200,160}, shade=10, tension=0.45, visc=0.10,
    cool={at=1413.8, to=54}, boil={at=3264.8, to=56}},
{id=56, key="SI_G", name="Кремний (г)", sym="Si", z=14, state=E.GAS, density=13, temp=3305, cond=0.40, cap=0.70, color={240,200,160}, shade=10, expand=0.0000060,
    cool={at=3264.8, to=55}},
{id=57, key="P", name="Фосфор", sym="P", z=15, state=E.POWDER, density=1969, temp=22, cond=0.15, cap=0.77, color={255,128,0}, shade=10, repose=0.55},
{id=58, key="S", name="Сера", sym="S", z=16, state=E.POWDER, density=2236, temp=22, cond=0.15, cap=0.71, color={255,255,48}, shade=10, repose=0.55,
    melt={at=115.2, to=59}},
{id=59, key="S_L", name="Сера (ж)", sym="S", z=16, state=E.LIQUID, density=2070, temp=280, cond=0.15, cap=0.71, color={255,255,48}, shade=10, tension=0.45, visc=0.10,
    cool={at=115.2, to=58}, boil={at=444.6, to=60}},
{id=60, key="S_G", name="Сера (г)", sym="S", z=16, state=E.GAS, density=14, temp=485, cond=0.15, cap=0.71, color={255,255,48}, shade=10, expand=0.0000060,
    cool={at=444.6, to=59}},
{id=61, key="CL", name="Хлор", sym="Cl", z=17, state=E.POWDER, density=1277, temp=-132, cond=0.15, cap=0.90, color={31,240,31}, shade=10, repose=0.55,
    melt={at=-101.5, to=62}},
{id=62, key="CL_L", name="Хлор (ж)", sym="Cl", z=17, state=E.LIQUID, density=1182, temp=-68, cond=0.15, cap=0.90, color={31,240,31}, shade=10, tension=0.45, visc=0.10,
    cool={at=-101.5, to=61}, boil={at=-34, to=63}},
{id=63, key="CL_G", name="Хлор (г)", sym="Cl", z=17, state=E.GAS, density=16, temp=22, cond=0.15, cap=0.90, color={31,240,31}, shade=10, expand=0.0000060,
    cool={at=-34, to=62}},
{id=64, key="AR_G", name="Аргон (г)", sym="Ar", z=18, state=E.GAS, density=18, temp=22, cond=0.05, cap=0.52, color={128,209,227}, shade=10, expand=0.0000060},
{id=65, key="K", name="Калий", sym="K", z=19, state=E.SOLID, density=931, temp=22, cond=0.85, cap=0.76, color={143,64,212}, shade=10,
    melt={at=63.6, to=66}},
{id=66, key="K_L", name="Калий (ж)", sym="K", z=19, state=E.LIQUID, density=862, temp=411, cond=0.85, cap=0.76, color={143,64,212}, shade=10, tension=0.45, visc=0.10,
    cool={at=63.6, to=65}, boil={at=758.9, to=67}},
{id=67, key="K_G", name="Калий (г)", sym="K", z=19, state=E.GAS, density=17, temp=799, cond=0.85, cap=0.76, color={143,64,212}, shade=10, expand=0.0000060,
    cool={at=758.9, to=66}},
{id=68, key="CA", name="Кальций", sym="Ca", z=20, state=E.SOLID, density=1674, temp=22, cond=0.85, cap=0.65, color={61,255,0}, shade=10,
    melt={at=841.9, to=69}},
{id=69, key="CA_L", name="Кальций (ж)", sym="Ca", z=20, state=E.LIQUID, density=1550, temp=1163, cond=0.85, cap=0.65, color={61,255,0}, shade=10, tension=0.45, visc=0.10,
    cool={at=841.9, to=68}, boil={at=1483.8, to=70}},
{id=70, key="CA_G", name="Кальций (г)", sym="Ca", z=20, state=E.GAS, density=18, temp=1524, cond=0.85, cap=0.65, color={61,255,0}, shade=10, expand=0.0000060,
    cool={at=1483.8, to=69}},
{id=71, key="SC", name="Скандий", sym="Sc", z=21, state=E.SOLID, density=3224, temp=22, cond=0.90, cap=0.57, color={230,230,230}, shade=10},
{id=72, key="TI", name="Титан", sym="Ti", z=22, state=E.SOLID, density=4866, temp=22, cond=0.90, cap=0.52, color={191,194,199}, shade=10,
    melt={at=1667.8, to=73}},
{id=73, key="TI_L", name="Титан (ж)", sym="Ti", z=22, state=E.LIQUID, density=4506, temp=2477, cond=0.90, cap=0.52, color={191,194,199}, shade=10, tension=0.45, visc=0.10,
    cool={at=1667.8, to=72}, boil={at=3286.8, to=74}},
{id=74, key="TI_G", name="Титан (г)", sym="Ti", z=22, state=E.GAS, density=21, temp=3327, cond=0.90, cap=0.52, color={191,194,199}, shade=10, expand=0.0000060,
    cool={at=3286.8, to=73}},
{id=75, key="V", name="Ванадий", sym="V", z=23, state=E.SOLID, density=6480, temp=22, cond=0.90, cap=0.49, color={166,166,171}, shade=10},
{id=76, key="CR", name="Хром", sym="Cr", z=24, state=E.SOLID, density=7765, temp=22, cond=0.90, cap=0.45, color={138,153,199}, shade=10},
{id=77, key="MN", name="Марганец", sym="Mn", z=25, state=E.SOLID, density=7787, temp=22, cond=0.90, cap=0.48, color={156,122,199}, shade=10},
{id=78, key="FE", name="Железо", sym="Fe", z=26, state=E.SOLID, density=8504, temp=22, cond=0.90, cap=0.45, color={224,102,51}, shade=10,
    melt={at=1537.8, to=79}},
{id=79, key="FE_L", name="Железо (ж)", sym="Fe", z=26, state=E.LIQUID, density=7874, temp=2199, cond=0.90, cap=0.45, color={224,102,51}, shade=10, tension=0.45, visc=0.10,
    cool={at=1537.8, to=78}, boil={at=2860.8, to=80}},
{id=80, key="FE_G", name="Железо (г)", sym="Fe", z=26, state=E.GAS, density=25, temp=2901, cond=0.90, cap=0.45, color={224,102,51}, shade=10, expand=0.0000060,
    cool={at=2860.8, to=79}},
{id=81, key="CO", name="Кобальт", sym="Co", z=27, state=E.SOLID, density=9612, temp=22, cond=0.90, cap=0.42, color={240,144,160}, shade=10},
{id=82, key="NI", name="Никель", sym="Ni", z=28, state=E.SOLID, density=9621, temp=22, cond=0.90, cap=0.44, color={80,208,80}, shade=10,
    melt={at=1454.8, to=83}},
{id=83, key="NI_L", name="Никель (ж)", sym="Ni", z=28, state=E.LIQUID, density=8908, temp=2092, cond=0.90, cap=0.44, color={80,208,80}, shade=10, tension=0.45, visc=0.10,
    cool={at=1454.8, to=82}, boil={at=2729.8, to=84}},
{id=84, key="NI_G", name="Никель (г)", sym="Ni", z=28, state=E.GAS, density=26, temp=2770, cond=0.90, cap=0.44, color={80,208,80}, shade=10, expand=0.0000060,
    cool={at=2729.8, to=83}},
{id=85, key="CU", name="Медь", sym="Cu", z=29, state=E.SOLID, density=9677, temp=22, cond=0.90, cap=0.38, color={200,128,51}, shade=10,
    melt={at=1084.6, to=86}},
{id=86, key="CU_L", name="Медь (ж)", sym="Cu", z=29, state=E.LIQUID, density=8960, temp=1823, cond=0.90, cap=0.38, color={200,128,51}, shade=10, tension=0.45, visc=0.10,
    cool={at=1084.6, to=85}, boil={at=2561.8, to=87}},
{id=87, key="CU_G", name="Медь (г)", sym="Cu", z=29, state=E.GAS, density=28, temp=2602, cond=0.90, cap=0.38, color={200,128,51}, shade=10, expand=0.0000060,
    cool={at=2561.8, to=86}},
{id=88, key="ZN", name="Цинк", sym="Zn", z=30, state=E.SOLID, density=7711, temp=22, cond=0.90, cap=0.39, color={125,128,176}, shade=10,
    melt={at=419.5, to=89}},
{id=89, key="ZN_L", name="Цинк (ж)", sym="Zn", z=30, state=E.LIQUID, density=7140, temp=663, cond=0.90, cap=0.39, color={125,128,176}, shade=10, tension=0.45, visc=0.10,
    cool={at=419.5, to=88}, boil={at=906.9, to=90}},
{id=90, key="ZN_G", name="Цинк (г)", sym="Zn", z=30, state=E.GAS, density=29, temp=947, cond=0.90, cap=0.39, color={125,128,176}, shade=10, expand=0.0000060,
    cool={at=906.9, to=89}},
{id=91, key="GA", name="Галлий", sym="Ga", z=31, state=E.SOLID, density=6383, temp=22, cond=0.80, cap=0.37, color={194,143,143}, shade=10},
{id=92, key="GE", name="Германий", sym="Ge", z=32, state=E.POWDER, density=5749, temp=22, cond=0.40, cap=0.32, color={102,143,143}, shade=10, repose=0.55},
{id=93, key="AS", name="Мышьяк", sym="As", z=33, state=E.POWDER, density=6185, temp=22, cond=0.40, cap=0.33, color={189,128,227}, shade=10, repose=0.55},
{id=94, key="SE", name="Селен", sym="Se", z=34, state=E.POWDER, density=5195, temp=22, cond=0.15, cap=0.32, color={255,161,0}, shade=10, repose=0.55},
{id=95, key="BR", name="Бром", sym="Br", z=35, state=E.POWDER, density=3351, temp=-37, cond=0.15, cap=0.90, color={166,41,41}, shade=10, repose=0.55,
    melt={at=-7.3, to=96}},
{id=96, key="BR_L", name="Бром (ж)", sym="Br", z=35, state=E.LIQUID, density=3103, temp=22, cond=0.15, cap=0.90, color={166,41,41}, shade=10, tension=0.45, visc=0.10,
    cool={at=-7.3, to=95}, boil={at=58.9, to=97}},
{id=97, key="BR_G", name="Бром (г)", sym="Br", z=35, state=E.GAS, density=36, temp=99, cond=0.15, cap=0.90, color={166,41,41}, shade=10, expand=0.0000060,
    cool={at=58.9, to=96}},
{id=98, key="KR_G", name="Криптон (г)", sym="Kr", z=36, state=E.GAS, density=37, temp=22, cond=0.05, cap=0.52, color={92,184,209}, shade=10, expand=0.0000060},
{id=99, key="RB", name="Рубидий", sym="Rb", z=37, state=E.SOLID, density=1655, temp=22, cond=0.85, cap=0.36, color={112,46,176}, shade=10},
{id=100, key="SR", name="Стронций", sym="Sr", z=38, state=E.SOLID, density=2851, temp=22, cond=0.85, cap=0.30, color={0,255,0}, shade=10},
{id=101, key="Y", name="Иттрий", sym="Y", z=39, state=E.SOLID, density=4830, temp=22, cond=0.90, cap=0.30, color={148,255,255}, shade=10},
{id=102, key="ZR", name="Цирконий", sym="Zr", z=40, state=E.SOLID, density=7042, temp=22, cond=0.90, cap=0.28, color={148,224,224}, shade=10},
{id=103, key="NB", name="Ниобий", sym="Nb", z=41, state=E.SOLID, density=9256, temp=22, cond=0.90, cap=0.26, color={115,194,201}, shade=10},
{id=104, key="MO", name="Молибден", sym="Mo", z=42, state=E.SOLID, density=11102, temp=22, cond=0.90, cap=0.25, color={84,181,181}, shade=10},
{id=105, key="TC", name="Технеций", sym="Tc", z=43, state=E.SOLID, density=11880, temp=22, cond=0.90, cap=0.25, color={59,158,158}, shade=10},
{id=106, key="RU", name="Рутений", sym="Ru", z=44, state=E.SOLID, density=13446, temp=22, cond=0.90, cap=0.24, color={36,143,143}, shade=10},
{id=107, key="RH", name="Родий", sym="Rh", z=45, state=E.SOLID, density=13403, temp=22, cond=0.90, cap=0.24, color={10,125,140}, shade=10},
{id=108, key="PD", name="Палладий", sym="Pd", z=46, state=E.SOLID, density=12985, temp=22, cond=0.90, cap=0.24, color={0,105,133}, shade=10},
{id=109, key="AG", name="Серебро", sym="Ag", z=47, state=E.SOLID, density=11329, temp=22, cond=0.90, cap=0.24, color={192,192,192}, shade=10,
    melt={at=961.8, to=110}},
{id=110, key="AG_L", name="Серебро (ж)", sym="Ag", z=47, state=E.LIQUID, density=10490, temp=1562, cond=0.90, cap=0.24, color={192,192,192}, shade=10, tension=0.45, visc=0.10,
    cool={at=961.8, to=109}, boil={at=2161.8, to=111}},
{id=111, key="AG_G", name="Серебро (г)", sym="Ag", z=47, state=E.GAS, density=48, temp=2202, cond=0.90, cap=0.24, color={192,192,192}, shade=10, expand=0.0000060,
    cool={at=2161.8, to=110}},
{id=112, key="CD", name="Кадмий", sym="Cd", z=48, state=E.SOLID, density=9342, temp=22, cond=0.90, cap=0.23, color={255,217,143}, shade=10},
{id=113, key="IN", name="Индий", sym="In", z=49, state=E.SOLID, density=7895, temp=22, cond=0.80, cap=0.23, color={166,117,115}, shade=10},
{id=114, key="SN", name="Олово", sym="Sn", z=50, state=E.SOLID, density=7954, temp=22, cond=0.80, cap=0.23, color={102,128,128}, shade=10,
    melt={at=231.9, to=115}},
{id=115, key="SN_L", name="Олово (ж)", sym="Sn", z=50, state=E.LIQUID, density=7365, temp=1417, cond=0.80, cap=0.23, color={102,128,128}, shade=10, tension=0.45, visc=0.10,
    cool={at=231.9, to=114}, boil={at=2601.8, to=116}},
{id=116, key="SN_G", name="Олово (г)", sym="Sn", z=50, state=E.GAS, density=53, temp=2642, cond=0.80, cap=0.23, color={102,128,128}, shade=10, expand=0.0000060,
    cool={at=2601.8, to=115}},
{id=117, key="SB", name="Сурьма", sym="Sb", z=51, state=E.POWDER, density=7233, temp=22, cond=0.40, cap=0.21, color={158,99,181}, shade=10, repose=0.55},
{id=118, key="TE", name="Теллур", sym="Te", z=52, state=E.POWDER, density=6739, temp=22, cond=0.40, cap=0.20, color={212,122,0}, shade=10, repose=0.55},
{id=119, key="I", name="Йод", sym="I", z=53, state=E.POWDER, density=5328, temp=22, cond=0.15, cap=0.90, color={148,0,148}, shade=10, repose=0.55,
    melt={at=113.7, to=120}},
{id=120, key="I_L", name="Йод (ж)", sym="I", z=53, state=E.LIQUID, density=4933, temp=149, cond=0.15, cap=0.90, color={148,0,148}, shade=10, tension=0.45, visc=0.10,
    cool={at=113.7, to=119}, boil={at=184.2, to=121}},
{id=121, key="I_G", name="Йод (г)", sym="I", z=53, state=E.GAS, density=57, temp=224, cond=0.15, cap=0.90, color={148,0,148}, shade=10, expand=0.0000060,
    cool={at=184.2, to=120}},
{id=122, key="XE_G", name="Ксенон (г)", sym="Xe", z=54, state=E.GAS, density=59, temp=22, cond=0.05, cap=0.52, color={66,158,176}, shade=10, expand=0.0000060},
{id=123, key="CS", name="Цезий", sym="Cs", z=55, state=E.SOLID, density=2084, temp=22, cond=0.85, cap=0.24, color={87,23,143}, shade=10},
{id=124, key="BA", name="Барий", sym="Ba", z=56, state=E.SOLID, density=3791, temp=22, cond=0.85, cap=0.20, color={0,201,0}, shade=10},
{id=125, key="LA", name="Лантан", sym="La", z=57, state=E.SOLID, density=6655, temp=22, cond=0.60, cap=0.20, color={112,212,255}, shade=10},
{id=126, key="CE", name="Церий", sym="Ce", z=58, state=E.SOLID, density=7312, temp=22, cond=0.60, cap=0.19, color={255,255,199}, shade=10},
{id=127, key="PR", name="Празеодим", sym="Pr", z=59, state=E.SOLID, density=7312, temp=22, cond=0.60, cap=0.19, color={217,255,199}, shade=10},
{id=128, key="ND", name="Неодим", sym="Nd", z=60, state=E.SOLID, density=7571, temp=22, cond=0.60, cap=0.19, color={199,255,199}, shade=10},
{id=129, key="PM", name="Прометий", sym="Pm", z=61, state=E.SOLID, density=7841, temp=22, cond=0.60, cap=0.45, color={163,255,199}, shade=10},
{id=130, key="SM", name="Самарий", sym="Sm", z=62, state=E.SOLID, density=8122, temp=22, cond=0.60, cap=0.20, color={143,255,199}, shade=10},
{id=131, key="EU", name="Европий", sym="Eu", z=63, state=E.SOLID, density=5685, temp=22, cond=0.60, cap=0.18, color={97,255,199}, shade=10},
{id=132, key="GD", name="Гадолиний", sym="Gd", z=64, state=E.SOLID, density=8532, temp=22, cond=0.60, cap=0.24, color={69,255,199}, shade=10},
{id=133, key="TB", name="Тербий", sym="Tb", z=65, state=E.SOLID, density=8888, temp=22, cond=0.60, cap=0.18, color={48,255,199}, shade=10},
{id=134, key="DY", name="Диспрозий", sym="Dy", z=66, state=E.SOLID, density=9223, temp=22, cond=0.60, cap=0.17, color={31,255,199}, shade=10},
{id=135, key="HO", name="Гольмий", sym="Ho", z=67, state=E.SOLID, density=9493, temp=22, cond=0.60, cap=0.16, color={0,255,156}, shade=10},
{id=136, key="ER", name="Эрбий", sym="Er", z=68, state=E.SOLID, density=9791, temp=22, cond=0.60, cap=0.17, color={0,230,117}, shade=10},
{id=137, key="TM", name="Тулий", sym="Tm", z=69, state=E.SOLID, density=10066, temp=22, cond=0.60, cap=0.16, color={0,212,82}, shade=10},
{id=138, key="YB", name="Иттербий", sym="Yb", z=70, state=E.SOLID, density=7452, temp=22, cond=0.60, cap=0.15, color={0,191,56}, shade=10},
{id=139, key="LU", name="Лютеций", sym="Lu", z=71, state=E.SOLID, density=10628, temp=22, cond=0.60, cap=0.15, color={0,171,36}, shade=10},
{id=140, key="HF", name="Гафний", sym="Hf", z=72, state=E.SOLID, density=14375, temp=22, cond=0.90, cap=0.14, color={77,194,255}, shade=10},
{id=141, key="TA", name="Тантал", sym="Ta", z=73, state=E.SOLID, density=18025, temp=22, cond=0.90, cap=0.14, color={77,166,255}, shade=10},
{id=142, key="W", name="Вольфрам", sym="W", z=74, state=E.SOLID, density=20790, temp=22, cond=0.90, cap=0.13, color={33,148,214}, shade=10},
{id=143, key="RE", name="Рений", sym="Re", z=75, state=E.SOLID, density=22702, temp=22, cond=0.90, cap=0.14, color={38,125,171}, shade=10},
{id=144, key="OS", name="Осмий", sym="Os", z=76, state=E.SOLID, density=24397, temp=22, cond=0.90, cap=0.13, color={38,102,150}, shade=10},
{id=145, key="IR", name="Иридий", sym="Ir", z=77, state=E.SOLID, density=24365, temp=22, cond=0.90, cap=0.13, color={23,84,135}, shade=10},
{id=146, key="PT", name="Платина", sym="Pt", z=78, state=E.SOLID, density=23166, temp=22, cond=0.90, cap=0.13, color={208,208,224}, shade=10},
{id=147, key="AU", name="Золото", sym="Au", z=79, state=E.SOLID, density=20844, temp=22, cond=0.90, cap=0.13, color={255,209,35}, shade=10,
    melt={at=1064.2, to=148}},
{id=148, key="AU_L", name="Золото (ж)", sym="Au", z=79, state=E.LIQUID, density=19300, temp=2017, cond=0.90, cap=0.13, color={255,209,35}, shade=10, tension=0.45, visc=0.10,
    cool={at=1064.2, to=147}, boil={at=2969.8, to=149}},
{id=149, key="AU_G", name="Золото (г)", sym="Au", z=79, state=E.GAS, density=88, temp=3010, cond=0.90, cap=0.13, color={255,209,35}, shade=10, expand=0.0000060,
    cool={at=2969.8, to=148}},
{id=150, key="HG", name="Ртуть", sym="Hg", z=80, state=E.SOLID, density=14617, temp=-69, cond=0.90, cap=0.14, color={184,184,208}, shade=10,
    melt={at=-38.8, to=151}},
{id=151, key="HG_L", name="Ртуть (ж)", sym="Hg", z=80, state=E.LIQUID, density=13534, temp=22, cond=0.90, cap=0.14, color={184,184,208}, shade=10, tension=0.45, visc=0.10,
    cool={at=-38.8, to=150}, boil={at=356.7, to=152}},
{id=152, key="HG_G", name="Ртуть (г)", sym="Hg", z=80, state=E.GAS, density=90, temp=397, cond=0.90, cap=0.14, color={184,184,208}, shade=10, expand=0.0000060,
    cool={at=356.7, to=151}},
{id=153, key="TL", name="Таллий", sym="Tl", z=81, state=E.SOLID, density=12798, temp=22, cond=0.80, cap=0.13, color={166,84,77}, shade=10},
{id=154, key="PB", name="Свинец", sym="Pb", z=82, state=E.SOLID, density=12247, temp=22, cond=0.80, cap=0.13, color={87,89,97}, shade=10,
    melt={at=327.5, to=155}},
{id=155, key="PB_L", name="Свинец (ж)", sym="Pb", z=82, state=E.LIQUID, density=11340, temp=1038, cond=0.80, cap=0.13, color={87,89,97}, shade=10, tension=0.45, visc=0.10,
    cool={at=327.5, to=154}, boil={at=1748.8, to=156}},
{id=156, key="PB_G", name="Свинец (г)", sym="Pb", z=82, state=E.GAS, density=93, temp=1789, cond=0.80, cap=0.13, color={87,89,97}, shade=10, expand=0.0000060,
    cool={at=1748.8, to=155}},
{id=157, key="BI", name="Висмут", sym="Bi", z=83, state=E.SOLID, density=10562, temp=22, cond=0.80, cap=0.12, color={158,79,181}, shade=10},
{id=158, key="PO", name="Полоний", sym="Po", z=84, state=E.SOLID, density=9932, temp=22, cond=0.80, cap=0.13, color={171,92,0}, shade=10},
{id=159, key="AT", name="Астат", sym="At", z=85, state=E.POWDER, density=6858, temp=22, cond=0.40, cap=0.45, color={117,79,69}, shade=10, repose=0.55},
{id=160, key="RN_G", name="Радон (г)", sym="Rn", z=86, state=E.GAS, density=99, temp=22, cond=0.05, cap=0.52, color={66,130,150}, shade=10, expand=0.0000060},
{id=161, key="FR", name="Франций", sym="Fr", z=87, state=E.SOLID, density=2020, temp=22, cond=0.85, cap=0.45, color={66,0,102}, shade=10},
{id=162, key="RA", name="Радий", sym="Ra", z=88, state=E.SOLID, density=5940, temp=22, cond=0.85, cap=0.45, color={0,125,0}, shade=10},
{id=163, key="AC", name="Актиний", sym="Ac", z=89, state=E.SOLID, density=10800, temp=22, cond=0.60, cap=0.12, color={112,171,250}, shade=10},
{id=164, key="TH", name="Торий", sym="Th", z=90, state=E.SOLID, density=12662, temp=22, cond=0.60, cap=0.11, color={0,186,255}, shade=10},
{id=165, key="PA", name="Протактиний", sym="Pa", z=91, state=E.SOLID, density=16600, temp=22, cond=0.60, cap=0.45, color={0,161,255}, shade=10},
{id=166, key="U", name="Уран", sym="U", z=92, state=E.SOLID, density=20628, temp=22, cond=0.60, cap=0.12, color={0,143,255}, shade=10,
    melt={at=1132.2, to=167}},
{id=167, key="U_L", name="Уран (ж)", sym="U", z=92, state=E.LIQUID, density=19100, temp=2632, cond=0.60, cap=0.12, color={0,143,255}, shade=10, tension=0.45, visc=0.10,
    cool={at=1132.2, to=166}},
{id=168, key="NP", name="Нептуний", sym="Np", z=93, state=E.SOLID, density=22086, temp=22, cond=0.60, cap=0.12, color={0,128,255}, shade=10},
{id=169, key="PU", name="Плутоний", sym="Pu", z=94, state=E.SOLID, density=21401, temp=22, cond=0.60, cap=0.15, color={0,107,255}, shade=10},
{id=170, key="AM", name="Америций", sym="Am", z=95, state=E.SOLID, density=12960, temp=22, cond=0.60, cap=0.26, color={84,92,242}, shade=10},
{id=171, key="CM", name="Кюрий", sym="Cm", z=96, state=E.SOLID, density=14591, temp=22, cond=0.60, cap=0.45, color={120,92,227}, shade=10},
{id=172, key="BK", name="Берклий", sym="Bk", z=97, state=E.SOLID, density=15962, temp=22, cond=0.60, cap=0.45, color={138,79,227}, shade=10},
{id=173, key="CF", name="Калифорний", sym="Cf", z=98, state=E.SOLID, density=16308, temp=22, cond=0.60, cap=0.45, color={161,54,212}, shade=10},
{id=174, key="ES", name="Эйнштейний", sym="Es", z=99, state=E.SOLID, density=9547, temp=22, cond=0.60, cap=0.45, color={179,31,212}, shade=10},
{id=175, key="FM", name="Фермий", sym="Fm", z=100, state=E.SOLID, density=10800, temp=22, cond=0.60, cap=0.45, color={179,31,186}, shade=10},
{id=176, key="MD", name="Менделевий", sym="Md", z=101, state=E.SOLID, density=10800, temp=22, cond=0.60, cap=0.45, color={179,13,166}, shade=10},
{id=177, key="NO", name="Нобелий", sym="No", z=102, state=E.SOLID, density=10800, temp=22, cond=0.60, cap=0.45, color={189,13,135}, shade=10},
{id=178, key="LR", name="Лоуренсий", sym="Lr", z=103, state=E.SOLID, density=10800, temp=22, cond=0.60, cap=0.45, color={199,0,102}, shade=10},
{id=179, key="RF", name="Резерфордий", sym="Rf", z=104, state=E.SOLID, density=25056, temp=22, cond=0.90, cap=0.45, color={204,0,89}, shade=10},
{id=180, key="DB", name="Дубний", sym="Db", z=105, state=E.SOLID, density=31644, temp=22, cond=0.90, cap=0.45, color={209,0,79}, shade=10},
{id=181, key="SG", name="Сиборгий", sym="Sg", z=106, state=E.SOLID, density=37800, temp=22, cond=0.90, cap=0.45, color={217,0,69}, shade=10},
{id=182, key="BH", name="Борий", sym="Bh", z=107, state=E.SOLID, density=40068, temp=22, cond=0.90, cap=0.45, color={224,0,56}, shade=10},
{id=183, key="HS_L", name="Хассий (ж)", sym="Hs", z=108, state=E.LIQUID, density=40700, temp=22, cond=0.90, cap=0.45, color={230,0,46}, shade=10, tension=0.45, visc=0.10},
{id=184, key="MT", name="Мейтнерий", sym="Mt", z=109, state=E.SOLID, density=40392, temp=22, cond=0.90, cap=0.45, color={235,0,38}, shade=10},
{id=185, key="DS", name="Дармштадтий", sym="Ds", z=110, state=E.SOLID, density=37584, temp=22, cond=0.90, cap=0.45, color={204,204,204}, shade=10},
{id=186, key="RG", name="Рентгений", sym="Rg", z=111, state=E.SOLID, density=30996, temp=22, cond=0.90, cap=0.45, color={204,204,204}, shade=10},
{id=187, key="CN", name="Коперниций", sym="Cn", z=112, state=E.SOLID, density=15120, temp=22, cond=0.90, cap=0.45, color={204,204,204}, shade=10},
{id=188, key="NH", name="Нихоний", sym="Nh", z=113, state=E.SOLID, density=17280, temp=22, cond=0.90, cap=0.45, color={204,204,204}, shade=10},
{id=189, key="FL", name="Флеровий", sym="Fl", z=114, state=E.SOLID, density=15120, temp=22, cond=0.80, cap=0.45, color={204,204,204}, shade=10},
{id=190, key="MC", name="Московий", sym="Mc", z=115, state=E.SOLID, density=14580, temp=22, cond=0.80, cap=0.45, color={204,204,204}, shade=10},
{id=191, key="LV", name="Ливерморий", sym="Lv", z=116, state=E.SOLID, density=13932, temp=22, cond=0.80, cap=0.45, color={204,204,204}, shade=10},
{id=192, key="TS", name="Теннессин", sym="Ts", z=117, state=E.POWDER, density=7744, temp=22, cond=0.40, cap=0.45, color={204,204,204}, shade=10, repose=0.55},
{id=193, key="OG", name="Оганесон", sym="Og", z=118, state=E.SOLID, density=5346, temp=22, cond=0.05, cap=0.52, color={204,204,204}, shade=10},
}

-- Соединения из compounds.lua попадают в тот же список: движку они такие же
-- вещества, просто с формулой и без номера в таблице Менделеева. Поле cmp
-- отличает их и от старых веществ, и от элементов (у тех есть z).
do
    local Cp = require("data.compounds")
    local id = E.list[#E.list].id
    local COND = {[E.SOLID]=0.55, [E.LIQUID]=0.30, [E.GAS]=0.10, [E.POWDER]=0.45}
    for _, c in ipairs(Cp.list) do
        id = id + 1
        c.id = id
        local e = {
            id = id, key = c.key, name = c.ru, formula = c.f, cmp = true,
            state = c.state, density = c.dens, temp = E.ROOM_TEMP,
            cond = COND[c.state], cap = 0.9, color = {c.r, c.g, c.b}, shade = 10,
        }
        if c.state == E.LIQUID then
            e.tension, e.visc = 0.45, 0.12
        elseif c.state == E.GAS then
            e.expand = 0.0000060
        end
        if c.flam then
            e.flam, e.ignite = c.flam, c.ign or 500
            e.residue = -1
        elseif c.ign then
            e.ignite = c.ign
        end
        if c.blast then e.blast, e.shock = c.blast, 0.6 end
        E.list[#E.list + 1] = e
        c.idx = #E.list
    end

    -- Пар для жидких соединений. До сих пор у соединений не было ни
    -- одного фазового перехода: в данных у спирта честно стоит кипение
    -- 78 градусов, а движок его не использовал — кипеть было некуда.
    -- Без пара нет и перегонки, а она отличает брагу от напитка.
    local vapors = {}
    for _, c in ipairs(Cp.list) do
        if c.state == E.LIQUID and c.boil and c.boil < 600 then
            id = id + 1
            local liq = E.list[c.idx]
            local v = {
                id = id, key = c.key .. "_G", name = c.ru .. ", пар",
                formula = c.f, cmp = true,
                state = E.GAS, density = math.max(2, math.floor(c.dens / 400)),
                temp = c.boil + 2, cond = 0.10, cap = 0.9,
                expand = 0.0000060, shade = 10,
                -- пар светлее своей жидкости, иначе их не различить
                color = { math.min(255, liq.color[1] + 45),
                          math.min(255, liq.color[2] + 45),
                          math.min(255, liq.color[3] + 45) },
            }
            if liq.flam then v.flam, v.ignite = liq.flam, liq.ignite end
            E.list[#E.list + 1] = v
            vapors[#vapors + 1] = { liq = liq, vap = v, boil = c.boil }
        end
    end
    -- Связываем обе стороны: жидкость кипит в пар, пар остывает обратно.
    -- Скрытая теплота обязательна: без неё вещество скачет через точку
    -- кипения туда-сюда каждый кадр и кипение выглядит мерцанием.
    for _, pr in ipairs(vapors) do
        pr.liq.boil = { at = pr.boil,     to = pr.vap.id }
        pr.liq.latV = 900
        pr.vap.cool = { at = pr.boil - 2, to = pr.liq.id }
        pr.vap.latV = 900
    end
end

E.byId, E.byKey = {}, {}
E.count = #E.list
for _, e in ipairs(E.list) do
    E.byId[e.id] = e
    E.byKey[e.key] = e.id
    e.disp = e.disp or 0
end

----------------------------------------------------------------------
-- Значения по умолчанию, зависящие от состояния.
local DEF = {
    [E.EMPTY]  = {grav=0, drag=1.000, elast=0.00, jitter=0.00, repose=0,
                  advec=0.00, airDrag=0.000, airLoss=1.00},
    [E.POWDER] = {grav=1, drag=0.970, elast=0.04, jitter=0.03, repose=0.85,
                  advec=0.30, airDrag=0.0015, airLoss=0.94},
    [E.LIQUID] = {grav=1, drag=0.930, elast=0.02, jitter=0.08, repose=1.00,
                  advec=0.45, airDrag=0.0020, airLoss=0.92},
    [E.GAS]    = {grav=1, drag=0.900, elast=0.25, jitter=0.35, repose=1.00,
                  advec=1.00, airDrag=0.0100, airLoss=0.99},
    [E.SOLID]  = {grav=0, drag=1.000, elast=0.00, jitter=0.00, repose=0,
                  advec=0.00, airDrag=0.000, airLoss=0.00},
}

-- Плоские таблицы Lua — для холодного кода, массивы FFI — для горячего.
E.stateOf, E.densityOf, E.tempOf, E.condOf = {}, {}, {}, {}
E.fixedOf, E.acidProofOf = {}, {}
E.meltAt, E.meltTo, E.boilAt, E.boilTo, E.coolAt, E.coolTo = {}, {}, {}, {}, {}, {}
E.igniteAt, E.flamOf, E.residueOf, E.lifeOf, E.decayToOf = {}, {}, {}, {}, {}
E.blastOf, E.shockOf, E.burnTempOf, E.burnLifeOf, E.burnToOf = {}, {}, {}, {}, {}
E.oxyUseOf, E.oxyNeedOf = {}, {}
E.activeArr = ffi.new("uint8_t[?]", E.count)

local names = {"tension","visc","state","dens","temp0","cond","cap","expans","latF","latV","grav","drag",
               "elast","jit","repose","advec","airDrag","airLoss","hotAir",
               "expand","fixed"}
for _, nm in ipairs(names) do E[nm .. "Arr"] = ffi.new("double[?]", E.count) end
E.densArr  = ffi.new("int32_t[?]", E.count)
E.stateArr = ffi.new("uint8_t[?]", E.count)
E.fixedArr = ffi.new("uint8_t[?]", E.count)

for i = 0, E.count - 1 do
    local e = E.byId[i]
    local d = DEF[e.state]

    E.stateOf[i]     = e.state
    E.densityOf[i]   = e.density
    E.tempOf[i]      = e.temp or E.ROOM_TEMP
    E.condOf[i]      = e.cond or 0.2
    E.fixedOf[i]     = e.fixed and 1 or 0
    E.acidProofOf[i] = e.acidProof and 1 or 0
    E.meltAt[i]      = e.melt and e.melt.at or 99999
    E.meltTo[i]      = e.melt and e.melt.to or 0
    E.boilAt[i]      = e.boil and e.boil.at or 99999
    E.boilTo[i]      = e.boil and e.boil.to or 0
    E.coolAt[i]      = e.cool and e.cool.at or -99999
    E.coolTo[i]      = e.cool and e.cool.to or 0
    E.igniteAt[i]    = e.ignite or 99999
    E.flamOf[i]      = e.flam or 0
    E.residueOf[i]   = e.residue or -1
    E.lifeOf[i]      = e.life or 0
    E.decayToOf[i]   = e.decayTo or 0
    E.blastOf[i]     = e.blast or 0
    E.shockOf[i]     = e.shock or 0
    E.burnTempOf[i]  = e.burnTemp or 900
    E.burnLifeOf[i]  = e.burnLife or 0
    E.burnToOf[i]    = e.burnTo or -1
    E.oxyUseOf[i]    = e.oxyUse or 0
    E.oxyNeedOf[i]   = e.oxyNeed or 0.10
    E.activeArr[i]   = e.active and 1 or 0

    E.stateArr[i]   = e.state
    E.densArr[i]    = e.density
    E.fixedArr[i]   = e.fixed and 1 or 0
    E.condArr[i]    = e.cond or 0.2
    E.capArr[i]     = e.cap or 1.0
    E.expansArr[i]  = e.expans or 0
    E.tensionArr[i] = e.tension or 0
    E.viscArr[i]    = e.visc or 0
    E.latFArr[i]    = e.latF or 0
    E.latVArr[i]    = e.latV or 0
    E.gravArr[i]    = e.grav    or d.grav
    E.dragArr[i]    = e.drag    or d.drag
    E.elastArr[i]   = e.elast   or d.elast
    E.jitArr[i]     = e.jitter  or d.jitter
    E.reposeArr[i]  = e.repose  or d.repose
    E.advecArr[i]   = e.advec   or d.advec
    E.airDragArr[i] = e.airDrag or d.airDrag
    E.airLossArr[i] = e.airLoss or d.airLoss
    E.hotAirArr[i]  = e.hotAir  or 0
    E.expandArr[i]  = e.expand  or 0
    E.temp0Arr[i]   = e.temp or E.ROOM_TEMP
    E.densArr[i]    = e.density
end

return E
