#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESET_CONFIG=0
AUTOSTART_MODE=preserve
FORCE_REBUILD=0
PLAN_ONLY=0

usage() {
  cat <<'USAGE'
Usage: scripts/install.sh [OPTIONS]

Options:
  --rebuild          Force a native rebuild even when source/install state matches.
  --plan             Report whether a rebuild is required; make no changes.
  --reset-config     Replace existing ClipReg config files with shipped defaults.
  --autostart        Enable ClipReg at COSMIC login.
  --no-autostart     Disable ClipReg login startup (commands may still start it on demand).
  -h, --help         Show this help.
USAGE
}

while (($#)); do
  case "$1" in
    --rebuild) FORCE_REBUILD=1 ;;
    --plan) PLAN_ONLY=1 ;;
    --reset-config) RESET_CONFIG=1 ;;
    --autostart) AUTOSTART_MODE=on ;;
    --no-autostart) AUTOSTART_MODE=off ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
[[ "$VERSION" =~ ^0\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]] || { echo "ERROR: invalid VERSION: $VERSION" >&2; exit 1; }

BIN_DIR="$HOME/.local/bin"
BIN="$BIN_DIR/clipreg"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/clipreg"
STATE_FILE="$STATE_DIR/build-state"
USER_SYSTEMD="$HOME/.config/systemd/user"
SERVICE_PATH="$USER_SYSTEMD/clipreg.service"

source_fingerprint() {
  (
    cd "$ROOT"
    LC_ALL=C find VERSION Makefile src protocol -type f -print0 \
      | LC_ALL=C sort -z \
      | xargs -0 sha256sum
  ) | sha256sum | awk '{print $1}'
}

read_state_value() {
  local key="$1"
  [[ -f "$STATE_FILE" ]] || return 0
  awk -F= -v k="$key" '$1==k {sub(/^[^=]*=/, ""); print; exit}' "$STATE_FILE"
}

write_build_state() {
  local source_hash="$1" binary_hash="$2"
  install -d -m 700 "$STATE_DIR"
  local tmp
  tmp="$(mktemp "$STATE_DIR/.build-state.XXXXXX")"
  trap 'rm -f "${tmp:-}"' RETURN
  {
    printf 'version=%s\n' "$VERSION"
    printf 'source_sha256=%s\n' "$source_hash"
    printf 'binary_sha256=%s\n' "$binary_hash"
  } > "$tmp"
  chmod 600 "$tmp"
  mv -f "$tmp" "$STATE_FILE"
  tmp=''
  trap - RETURN
}

PREVIOUS_SERVICE=0
WAS_ENABLED=0
if [[ -e "$SERVICE_PATH" ]]; then
  PREVIOUS_SERVICE=1
  systemctl --user is-enabled --quiet clipreg.service 2>/dev/null && WAS_ENABLED=1 || true
fi

SOURCE_HASH="$(source_fingerprint)"
INSTALLED_VERSION=""
INSTALLED_HASH=""
[[ -x "$BIN" ]] && INSTALLED_VERSION="$($BIN --version 2>/dev/null || true)"
[[ -x "$BIN" ]] && INSTALLED_HASH="$(sha256sum "$BIN" | awk '{print $1}')"
STATE_VERSION="$(read_state_value version)"
STATE_SOURCE_HASH="$(read_state_value source_sha256)"
STATE_BINARY_HASH="$(read_state_value binary_sha256)"

REBUILD="$FORCE_REBUILD"
REBUILD_REASONS=()
((FORCE_REBUILD)) && REBUILD_REASONS+=(forced)
if [[ ! -x "$BIN" ]]; then
  REBUILD=1; REBUILD_REASONS+=(binary-missing)
else
  [[ "$INSTALLED_VERSION" == "$VERSION" ]] || { REBUILD=1; REBUILD_REASONS+=(version-mismatch); }
  [[ "$STATE_VERSION" == "$VERSION" ]] || { REBUILD=1; REBUILD_REASONS+=(state-version-mismatch); }
  [[ "$STATE_SOURCE_HASH" == "$SOURCE_HASH" ]] || { REBUILD=1; REBUILD_REASONS+=(source-mismatch); }
  [[ -n "$INSTALLED_HASH" && "$STATE_BINARY_HASH" == "$INSTALLED_HASH" ]] || { REBUILD=1; REBUILD_REASONS+=(binary-hash-mismatch); }
fi

if ((PLAN_ONLY)); then
  if ((REBUILD)); then
    printf 'rebuild=yes\nversion=%s\nsource_sha256=%s\nreasons=%s\n' \
      "$VERSION" "$SOURCE_HASH" "$(IFS=,; echo "${REBUILD_REASONS[*]}")"
  else
    printf 'rebuild=no\nversion=%s\nsource_sha256=%s\n' "$VERSION" "$SOURCE_HASH"
  fi
  exit 0
fi

# Runtime helper dependencies are cheap to verify on every real install. Native
# build dependencies are only needed when a rebuild is actually required.
need_runtime=()
command -v notify-send >/dev/null 2>&1 || need_runtime+=(libnotify-bin)
if ((${#need_runtime[@]})); then
  sudo apt-get update
  sudo apt-get install -y "${need_runtime[@]}"
fi

if ((REBUILD)); then
  need_build=()
  command -v cc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 || need_build+=(build-essential)
  command -v pkg-config >/dev/null 2>&1 || need_build+=(pkg-config)
  command -v wayland-scanner >/dev/null 2>&1 || need_build+=(libwayland-bin)
  pkg-config --exists wayland-client 2>/dev/null || need_build+=(libwayland-dev)
  if ((${#need_build[@]})); then
    sudo apt-get update
    sudo apt-get install -y "${need_build[@]}"
  fi

  echo "[BUILD] ClipReg $VERSION (source $SOURCE_HASH)"
  make -C "$ROOT" clean selftest

  install -d "$BIN_DIR"
  tmp="$(mktemp "$BIN_DIR/.clipreg.new.XXXXXX")"
  trap 'rm -f "${tmp:-}"' EXIT
  install -m 755 "$ROOT/build/clipreg" "$tmp"
  mv -f "$tmp" "$BIN"
  tmp=''
  trap - EXIT

  INSTALLED_HASH="$(sha256sum "$BIN" | awk '{print $1}')"
  write_build_state "$SOURCE_HASH" "$INSTALLED_HASH"
else
  echo "[REUSE] ClipReg $VERSION binary matches source/install fingerprint"
fi

# v0.1.0 stored registers in CopyQ. Migration is read-only, idempotent, and
# deliberately leaves the original CopyQ tab intact as a recovery source.
python3 "$ROOT/scripts/migrate-v1.py"

install -d "$BIN_DIR"
install -m 755 "$ROOT/scripts/clipreg-keybinds" "$BIN_DIR/clipreg-keybinds"

CFG="${XDG_CONFIG_HOME:-$HOME/.config}/clipreg"
install -d -m 700 "$CFG"
for name in clipreg.conf app-profiles.conf keymap.toml; do
  if ((RESET_CONFIG)) || [[ ! -e "$CFG/$name" ]]; then
    install -m 600 "$ROOT/config/$name" "$CFG/$name"
  else
    echo "[KEEP] $CFG/$name"
  fi
done

install -d "$USER_SYSTEMD"
install -m 644 "$ROOT/systemd/clipreg.service" "$SERVICE_PATH"
sudo install -m 644 "$ROOT/udev/70-clipreg-uinput.rules" /etc/udev/rules.d/70-clipreg-uinput.rules
printf '%s\n' uinput | sudo tee /etc/modules-load.d/clipreg-uinput.conf >/dev/null
sudo modprobe uinput
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=misc --sysname-match=uinput || true
sudo udevadm settle || true

systemctl --user daemon-reload
case "$AUTOSTART_MODE" in
  on) desired=1 ;;
  off) desired=0 ;;
  preserve)
    if ((PREVIOUS_SERVICE)); then desired=$WAS_ENABLED; else desired=1; fi
    ;;
esac

if ((desired)); then
  systemctl --user enable clipreg.service >/dev/null
  # Restart after a rebuild or service-unit refresh so the running daemon cannot
  # remain on an older executable. Startup-disabled installs stay stopped.
  systemctl --user restart clipreg.service || true
else
  systemctl --user disable --now clipreg.service >/dev/null 2>&1 || true
fi

echo "ClipReg $($BIN --version) installed"
