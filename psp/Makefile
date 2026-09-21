# ARGUS — обёртка над инструментами. Цели: test, assets, psp, run-emu, clean.
PSPDEV          ?= $(HOME)/pspdev
PPSSPP_HEADLESS ?= $(HOME)/ppsspp/PPSSPPHeadless
BUILD           ?= build
BUILD_TYPE      ?= Release
TOOLCHAIN       := $(PSPDEV)/psp/share/pspdev.cmake
SCRIPT          ?= tests/autoplay/smoke.txt
SECONDS         ?= 20
HOST_CC         ?= cc
HOST_CFLAGS     := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Icore -Itests
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
	@echo "EBOOT: $(BUILD)/ARGUS/EBOOT.PBP ($$(du -h $(BUILD)/ARGUS/EBOOT.PBP | cut -f1))"

test: assets
	mkdir -p $(BUILD)/host
	$(HOST_CC) $(HOST_CFLAGS) core/*.c tests/platform_stub.c tests/unit/test_core.c -o $(BUILD)/host/test_core -lm
	ARGUS_ASSETS=$(abspath $(BUILD)/assets) $(BUILD)/host/test_core

run-emu: psp
	python3 tools/run_emu.py --emu $(PPSSPP_HEADLESS) --game $(BUILD)/ARGUS --script $(SCRIPT) --seconds $(SECONDS) --out $(BUILD)/shots

clean:
	rm -rf $(BUILD)
