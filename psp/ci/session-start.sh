#!/usr/bin/env bash
# Бутстрап веб-сессии Claude Code для psp/: тулчейн pspdev, Python-зависимости, эмулятор.
# Идемпотентен. Вызывается SessionStart-хуком или вручную: bash psp/ci/session-start.sh
set -u
PSPDEV="${PSPDEV:-$HOME/pspdev}"
TOOLCHAIN_URL="https://github.com/pspdev/pspdev/releases/download/latest/pspdev-ubuntu-latest-x86_64.tar.gz"
CACHE_DIR="${ARGUS_CACHE:-$HOME/.cache/argus}"
mkdir -p "$CACHE_DIR"

log() { echo "[session-start] $*"; }

if [ ! -x "$PSPDEV/bin/psp-gcc" ]; then
    log "тулчейн не найден, скачиваю $TOOLCHAIN_URL"
    if curl -fL --retry 4 --retry-delay 3 -o "$CACHE_DIR/pspdev.tar.gz" "$TOOLCHAIN_URL"; then
        rm -rf "$CACHE_DIR/extract" && mkdir -p "$CACHE_DIR/extract"
        tar -xzf "$CACHE_DIR/pspdev.tar.gz" -C "$CACHE_DIR/extract"
        rm -rf "$PSPDEV"
        if [ -d "$CACHE_DIR/extract/pspdev" ]; then mv "$CACHE_DIR/extract/pspdev" "$PSPDEV"; else mv "$CACHE_DIR/extract" "$PSPDEV"; fi
        rm -f "$CACHE_DIR/pspdev.tar.gz"
        log "тулчейн установлен: $("$PSPDEV/bin/psp-gcc" --version | head -1)"
    else
        log "ОШИБКА: не удалось скачать тулчейн (сеть/прокси)"
    fi
else
    log "тулчейн уже есть: $("$PSPDEV/bin/psp-gcc" --version | head -1)"
fi

if ! python3 -c "import PIL, numpy, fontTools" 2>/dev/null; then
    log "ставлю pillow numpy fonttools"
    python3 -m pip install -q --user pillow numpy fonttools 2>&1 | grep -v WARNING || true
fi

PPSSPP_HEADLESS="${PPSSPP_HEADLESS:-$HOME/ppsspp/PPSSPPHeadless}"
if [ ! -x "$PPSSPP_HEADLESS" ]; then
    log "PPSSPPHeadless не найден ($PPSSPP_HEADLESS); собери: bash psp/ci/build-ppsspp.sh"
fi

# Экспорт переменных в окружение сессии Claude Code (если хук предоставил файл)
if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
    {
        echo "export PSPDEV=$PSPDEV"
        echo "export PATH=$PSPDEV/bin:\$PATH"
        echo "export PPSSPP_HEADLESS=$PPSSPP_HEADLESS"
    } >> "$CLAUDE_ENV_FILE"
fi
log "готово"
