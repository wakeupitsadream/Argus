#!/bin/bash
# SessionStart-хук Claude Code на вебе: готовит окружение для psp/ (тулчейн pspdev, Python-зависимости).
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

bash "$CLAUDE_PROJECT_DIR/psp/ci/session-start.sh"
