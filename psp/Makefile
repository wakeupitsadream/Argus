# ARGUS — обёртка над инструментами. Цели: test, assets, psp, run-emu, clean.
PSPDEV          ?= $(HOME)/pspdev
PPSSPP_HEADLESS ?= $(HOME)/ppsspp/PPSSPPHeadless
BUILD           ?= build
BUILD_TYPE      ?= Release
TOOLCHAIN       := $(PSPDEV)/psp/share/pspdev.cmake
SCRIPT          ?= tests/autoplay/smoke.txt
SECONDS         ?= 20
HOST_CC         ?= cc
# Модули, ещё не подключённые к игре (в работе) — исключаем и из хост-сборки.
CORE_SRC        := $(filter-out core/entity.c core/puzzle_beam.c core/puzzle_mech.c core/quest.c,$(wildcard core/*.c))
TEST_SRC        := $(filter-out tests/unit/test_entity.c tests/unit/test_beam.c tests/unit/test_mech.c tests/unit/test_quest.c,$(wildcard tests/unit/*.c))
HOST_CFLAGS     := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Icore -Itests -Ibuild/assets
PSP_ENV         := PSPDEV=$(PSPDEV) PATH=$(PSPDEV)/bin:$$PATH

.PHONY: all assets psp test run-emu clean check-toolchain

all: test assets psp

check-toolchain:
	@test -x $(PSPDEV)/bin/psp-gcc || { echo "psp-gcc не найден в $(PSPDEV)/bin — запусти ci/session-start.sh"; exit 1; }

assets:
	python3 tools/mkassets.py $(BUILD)/assets

psp: check-toolchain assets
	$(PSP_ENV) cmake -S . -B $(BUILD)/psp -G Ninja -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DARGUS_ASSETS=$(abspath $(BUILD)/assets)
	$(PSP_ENV) cmake --build $(BUILD)/psp
	rm -rf $(BUILD)/ARGUS && mkdir -p $(BUILD)/ARGUS/data
	cp $(BUILD)/psp/EBOOT.PBP $(BUILD)/ARGUS/
	cp $(BUILD)/assets/*.msh $(BUILD)/assets/*.pal $(BUILD)/ARGUS/data/
	-cp $(BUILD)/assets/*.lvl $(BUILD)/assets/font.bin $(BUILD)/assets/strings_*.bin $(BUILD)/ARGUS/data/ 2>/dev/null || true
	@echo "EBOOT: $(BUILD)/ARGUS/EBOOT.PBP ($$(du -h $(BUILD)/ARGUS/EBOOT.PBP | cut -f1))"

test: assets
	mkdir -p $(BUILD)/host
	$(HOST_CC) $(HOST_CFLAGS) $(CORE_SRC) tests/platform_stub.c $(TEST_SRC) -o $(BUILD)/host/test_core -lm
	ARGUS_ASSETS=$(abspath $(BUILD)/assets) $(BUILD)/host/test_core

run-emu: psp
	python3 tools/run_emu.py --emu $(PPSSPP_HEADLESS) --game $(BUILD)/ARGUS --script $(SCRIPT) --seconds $(SECONDS) --out $(BUILD)/shots

clean:
	rm -rf $(BUILD)
