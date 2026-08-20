#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
need=()
command -v copyq >/dev/null 2>&1 || need+=(copyq)
command -v wtype >/dev/null 2>&1 || need+=(wtype)
command -v flock >/dev/null 2>&1 || need+=(util-linux)
command -v python3 >/dev/null 2>&1 || need+=(python3)
if ((${#need[@]})); then
  sudo apt-get update
  sudo apt-get install -y "${need[@]}"
fi
install -d "$HOME/.local/bin"
install -m 755 "$ROOT/bin/clipreg" "$HOME/.local/bin/clipreg"
install -m 755 "$ROOT/scripts/clipreg-keybinds" "$HOME/.local/bin/clipreg-keybinds"
echo "ClipReg $($HOME/.local/bin/clipreg --version) installed"
echo "Run: clipreg-keybinds"
