#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
PURGE=0
[[ "${1:-}" == '--purge-data' ]] && PURGE=1
systemctl --user disable --now clipreg.service >/dev/null 2>&1 || true
rm -f "$HOME/.config/systemd/user/clipreg.service" "$HOME/.local/bin/clipreg" "$HOME/.local/bin/clipreg-keybinds"
systemctl --user daemon-reload
sudo rm -f /etc/udev/rules.d/70-clipreg-uinput.rules /etc/modules-load.d/clipreg-uinput.conf
sudo udevadm control --reload-rules || true
if ((PURGE)); then rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/clipreg" "${XDG_DATA_HOME:-$HOME/.local/share}/clipreg"; fi
echo 'ClipReg uninstalled. Persistent config/registers preserved unless --purge-data was specified.'
