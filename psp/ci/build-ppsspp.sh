#!/usr/bin/env bash
# Сборка PPSSPPHeadless из исходников (Linux x86_64, без SDL, программный рендер).
# Результат: $PPSSPP_HEADLESS (по умолчанию ~/ppsspp/PPSSPPHeadless). Занимает 10–20 минут на 4 ядрах.
#
# Почему патчи: master PPSSPP собирает headless только с SDL3, которого нет в Ubuntu 24.04.
# Режим HEADLESS_CROSS отключает SDL-фронтенд, но два guard'а в headless/Headless.cpp знают
# только про loongarch64/riscv64 — расширяем их на «нет SDL» (нам нужен только --graphics=software).
set -euo pipefail
WORK="${PPSSPP_SRC:-$HOME/ppsspp-src}"
OUT="${PPSSPP_HEADLESS:-$HOME/ppsspp/PPSSPPHeadless}"
JOBS="${JOBS:-$(nproc)}"

if [ ! -d "$WORK/.git" ]; then
    git clone --depth 1 --recursive --shallow-submodules https://github.com/hrydgard/ppsspp.git "$WORK"
fi
cd "$WORK"
echo "PPSSPP $(git log -1 --format='%h %ad' --date=short)"

# Патчи guard'ов (идемпотентно)
sed -i 's/#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_ARCH(LOONGARCH64) || PPSSPP_ARCH(RISCV64)$/#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_ARCH(LOONGARCH64) || PPSSPP_ARCH(RISCV64) || !defined(SDL)/' headless/Headless.cpp
sed -i 's/^#elif PPSSPP_ARCH(LOONGARCH64) || PPSSPP_ARCH(RISCV64)$/#elif PPSSPP_ARCH(LOONGARCH64) || PPSSPP_ARCH(RISCV64) || !defined(SDL)/' headless/Headless.cpp
grep -c "!defined(SDL)" headless/Headless.cpp >/dev/null || { echo "патч не применился — проверь headless/Headless.cpp"; exit 1; }

cmake -S . -B build-headless -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DHEADLESS=ON -DHEADLESS_CROSS=ON -DUSE_FFMPEG=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
    -DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=OFF -DUSE_SYSTEM_LIBPNG=OFF -Wno-dev
cmake --build build-headless --target PPSSPPHeadless -j "$JOBS"
mkdir -p "$(dirname "$OUT")"
cp build-headless/PPSSPPHeadless "$OUT"
# assets нужны эмулятору для шрифтов/языков; кладём рядом
rm -rf "$(dirname "$OUT")/assets" && cp -r assets "$(dirname "$OUT")/assets"
echo "готово: $OUT"
