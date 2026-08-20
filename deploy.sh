#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEYBINDS_MODE=ask
RUN_KEYBINDS=1
PLAN_ONLY=0
INSTALL_ARGS=()

usage() {
  cat <<'USAGE'
Usage: ./deploy.sh [OPTIONS]

Reconcile ClipReg from this source tree. Native compilation occurs only when
build identity/source/install state requires it unless --rebuild is explicit.

Options:
  --plan                 Report rebuild/install plan; make no changes.
  --rebuild              Force native rebuild.
  --reset-config         Replace user config with shipped defaults.
  --autostart            Enable startup at COSMIC login.
  --no-autostart         Disable startup at COSMIC login.
  --keybinds MODE        ask, choose, digits, functions, both,
                         set:NAME[,NAME...], all, check, remove, or skip.
  --no-keybinds          Do not touch COSMIC keybindings.
  -h, --help             Show help.
USAGE
}

while (($#)); do
  case "$1" in
    --plan) PLAN_ONLY=1 ;;
    --rebuild|--reset-config|--autostart|--no-autostart) INSTALL_ARGS+=("$1") ;;
    --keybinds) shift; (($#)) || { echo 'ERROR: --keybinds requires MODE' >&2; exit 2; }; KEYBINDS_MODE="$1" ;;
    --no-keybinds) RUN_KEYBINDS=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if ((PLAN_ONLY)); then
  exec "$ROOT/scripts/install.sh" --plan "${INSTALL_ARGS[@]}"
fi

"$ROOT/scripts/install.sh" "${INSTALL_ARGS[@]}"
if ((RUN_KEYBINDS)); then
  "$HOME/.local/bin/clipreg-keybinds" "$KEYBINDS_MODE"
fi
"$HOME/.local/bin/clipreg" doctor
