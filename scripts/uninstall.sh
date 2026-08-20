#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
PURGE=0
[[ "${1:-}" == '--purge-data' ]] && PURGE=1

if [[ -x "$HOME/.local/bin/clipreg-keybinds" ]]; then
  "$HOME/.local/bin/clipreg-keybinds" remove >/dev/null 2>&1 || true
fi
systemctl --user disable --now clipreg.service >/dev/null 2>&1 || true
rm -f "$HOME/.config/systemd/user/clipreg.service" "$HOME/.local/bin/clipreg" "$HOME/.local/bin/clipreg-keybinds"
systemctl --user daemon-reload
sudo rm -f /etc/udev/rules.d/70-clipreg-uinput.rules /etc/modules-load.d/clipreg-uinput.conf
sudo udevadm control --reload-rules || true
[[ -n "${XDG_RUNTIME_DIR:-}" ]] && rm -rf "$XDG_RUNTIME_DIR/clipreg"
if ((PURGE)); then
  rm -rf \
    "${XDG_CONFIG_HOME:-$HOME/.config}/clipreg" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/clipreg" \
    "${XDG_STATE_HOME:-$HOME/.local/state}/clipreg"
fi
echo 'ClipReg uninstalled. Persistent config/registers/build state preserved unless --purge-data was specified.'
