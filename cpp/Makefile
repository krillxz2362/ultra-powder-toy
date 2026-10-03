# Сборка ядра и проверок. CMake не нужен: три файла.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
# Ядро разложено по модулям: у каждого свой договор, см. МОДУЛИ.md.
#   world   — частицы, карта клеток, пробуждение, кадр
#   contact — опора, удар, скольжение
#   motion  — тяжесть, давление, ветер, перенос
#   state   — тепло, фазы, срок жизни
#   chem    — поиск пар, проверка, правила, исполнение
SRC       = src/world.cpp src/contact.cpp src/motion.cpp src/state.cpp src/chem.cpp
# Заголовки — тоже зависимость. Без них make оставляет старый двоичный
# файл, и сверка показывает числа прошлой сборки. Такая ловушка уже
# один раз дала ложный замер.
HDR       = include/upt_core.h include/upt_substances.h include/upt_chem.h
BINS      = heat_ref heat_bench move_ref move_bench water_ref water_bench \
            full_ref full_bench still_ref fall_ref ui_ref save_ref level_ref press_ref melt_ref

all: $(BINS)

heat_ref: tests/heat_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

move_ref: tests/move_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

heat_bench: tests/heat_bench.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

move_bench: tests/move_bench.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

water_ref: tests/water_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

water_bench: tests/water_bench.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

full_ref: tests/full_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

full_bench: tests/full_bench.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

melt_ref: tests/melt_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

press_ref: tests/press_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

level_ref: tests/level_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

save_ref: tests/save_ref.cpp src/save.cpp $(SRC) $(HDR) include/upt_save.h
	$(CXX) $(CXXFLAGS) tests/save_ref.cpp src/save.cpp $(SRC) -o $@

ui_ref: tests/ui_ref.cpp src/ui.cpp $(SRC) $(HDR) include/upt_ui.h include/upt_font.h
	$(CXX) $(CXXFLAGS) tests/ui_ref.cpp src/ui.cpp $(SRC) -o $@

still_ref: tests/still_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

fall_ref: tests/fall_ref.cpp $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@

# Сверка с эталоном на Lua: числа должны совпасть до шестого знака.
check: heat_ref move_ref water_ref full_ref still_ref ui_ref save_ref
	cd .. && python3 run.py tools/heat_ref.lua
	./heat_ref
	cd .. && python3 run.py tools/move_ref.lua
	./move_ref
	cd .. && python3 run.py tools/water_ref.lua
	./water_ref
	cd .. && python3 run.py tools/full_ref.lua
	./full_ref
	cd .. && python3 run.py tools/still_ref.lua
	./still_ref
	./ui_ref
	./save_ref
	./press_ref
	./melt_ref

bench: heat_bench move_bench water_bench
	cd .. && python3 run.py /tmp/heat_bench.lua || true
	./heat_bench
	./move_bench
	./water_bench
	./full_bench

# Окно и ввод. SDL2 собран из исходников в ~/.sdl2 — пакетом его без
# прав администратора не поставить. Цель отдельная: без SDL2 всё
# остальное обязано собираться.
SDL2_CONFIG ?= $(HOME)/.sdl2/bin/sdl2-config

# Заголовки стоят в зависимостях, но в компилятор идут только исходники:
# через $^ в командную строку попадали и .h, и g++ собирал из них
# предкомпилированный заголовок вместо программы.
upt: src/main_sdl.cpp src/ui.cpp src/save.cpp $(SRC) $(HDR) include/upt_ui.h include/upt_font.h include/upt_save.h
	@test -f $(SDL2_CONFIG) || { echo "нет $(SDL2_CONFIG) — собери SDL2"; exit 1; }
	$(CXX) $(CXXFLAGS) `sh $(SDL2_CONFIG) --cflags` \
	    -DUPT_WITH_SDL src/main_sdl.cpp src/ui.cpp src/save.cpp $(SRC) -o $@ `sh $(SDL2_CONFIG) --static-libs`

# Снимок окна без экрана: так интерфейс видно, не запуская его вживую.
shot: upt
	./upt --снимок --мир 300 170 --масштаб 3
	python3 -c "from PIL import Image; Image.open('/tmp/upt_окно.bmp').save('../snapshots/интерфейс.png')"

clean:
	rm -f $(BINS) upt
