#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEYBINDS_MODE=ask
RUN_KEYBINDS=1
INSTALL_ARGS=()

usage() {
  cat <<'USAGE'
Usage: ./deploy.sh [OPTIONS]

Build/install ClipReg from this source tree, configure COSMIC shortcuts, and
run diagnostics. The native binary is rebuilt only when required unless forced.

Options:
  --rebuild              Force native rebuild.
  --reset-config         Replace user config with shipped defaults.
  --autostart            Enable startup at COSMIC login.
  --no-autostart         Disable startup at COSMIC login.
  --keybinds MODE        Keybinding mode: ask, choose, digits, functions, both,
                         set:NAME[,NAME...], all, check, remove, or skip.
  --no-keybinds          Do not touch COSMIC keybindings.
  -h, --help             Show this help.
USAGE
}

while (($#)); do
  case "$1" in
    --rebuild|--reset-config|--autostart|--no-autostart)
      INSTALL_ARGS+=("$1")
      ;;
    --keybinds)
      shift; (($#)) || { echo 'ERROR: --keybinds requires MODE' >&2; exit 2; }
      KEYBINDS_MODE="$1"
      ;;
    --no-keybinds) RUN_KEYBINDS=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

"$ROOT/scripts/install.sh" "${INSTALL_ARGS[@]}"
if ((RUN_KEYBINDS)); then
  "$HOME/.local/bin/clipreg-keybinds" "$KEYBINDS_MODE"
fi
"$HOME/.local/bin/clipreg" doctor
