#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESET_CONFIG=0
AUTOSTART_MODE=preserve
while (($#)); do
  case "$1" in
    --reset-config) RESET_CONFIG=1 ;;
    --autostart) AUTOSTART_MODE=on ;;
    --no-autostart) AUTOSTART_MODE=off ;;
    -h|--help) echo 'Usage: scripts/install.sh [--reset-config] [--autostart|--no-autostart]'; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
USER_SYSTEMD="$HOME/.config/systemd/user"
SERVICE_PATH="$USER_SYSTEMD/clipreg.service"
PRE=0; WAS=0
if [[ -e "$SERVICE_PATH" ]]; then PRE=1; systemctl --user is-enabled --quiet clipreg.service 2>/dev/null && WAS=1 || true; fi
need=()
command -v cc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 || need+=(build-essential)
command -v pkg-config >/dev/null 2>&1 || need+=(pkg-config)
command -v wayland-scanner >/dev/null 2>&1 || need+=(libwayland-bin)
pkg-config --exists wayland-client 2>/dev/null || need+=(libwayland-dev)
command -v notify-send >/dev/null 2>&1 || need+=(libnotify-bin)
if ((${#need[@]})); then sudo apt-get update; sudo apt-get install -y "${need[@]}"; fi
make -C "$ROOT" clean selftest
python3 "$ROOT/scripts/migrate-v1.py"
install -d "$HOME/.local/bin"
tmp="$(mktemp "$HOME/.local/bin/.clipreg.new.XXXXXX")"; trap 'rm -f "${tmp:-}"' EXIT
install -m 755 "$ROOT/build/clipreg" "$tmp"; mv -f "$tmp" "$HOME/.local/bin/clipreg"; tmp=''; trap - EXIT
install -m 755 "$ROOT/scripts/clipreg-keybinds" "$HOME/.local/bin/clipreg-keybinds"
CFG="${XDG_CONFIG_HOME:-$HOME/.config}/clipreg"; install -d -m 700 "$CFG"
for name in clipreg.conf app-profiles.conf keymap.toml; do
  if ((RESET_CONFIG)) || [[ ! -e "$CFG/$name" ]]; then install -m 600 "$ROOT/config/$name" "$CFG/$name"; else echo "[KEEP] $CFG/$name"; fi
done
install -d "$USER_SYSTEMD"; install -m 644 "$ROOT/systemd/clipreg.service" "$SERVICE_PATH"
sudo install -m 644 "$ROOT/udev/70-clipreg-uinput.rules" /etc/udev/rules.d/70-clipreg-uinput.rules
printf '%s\n' uinput | sudo tee /etc/modules-load.d/clipreg-uinput.conf >/dev/null
sudo modprobe uinput; sudo udevadm control --reload-rules; sudo udevadm trigger --action=change --subsystem-match=misc --sysname-match=uinput || true; sudo udevadm settle || true
systemctl --user daemon-reload
case "$AUTOSTART_MODE" in on) desired=1;; off) desired=0;; preserve) if ((PRE)); then desired=$WAS; else desired=1; fi;; esac
if ((desired)); then systemctl --user enable clipreg.service >/dev/null; systemctl --user restart clipreg.service || true; else systemctl --user disable --now clipreg.service >/dev/null 2>&1 || true; fi
echo "ClipReg $($HOME/.local/bin/clipreg --version) installed"
