#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
"$HOME/.local/bin/clipreg-keybinds" remove 2>/dev/null || true
rm -f "$HOME/.local/bin/clipreg" "$HOME/.local/bin/clipreg-keybinds"
echo 'ClipReg v0.1.x executables removed. The CopyQ Clipboard Registers tab is preserved.'
