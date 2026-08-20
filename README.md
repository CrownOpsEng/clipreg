# ClipReg

Transactional persistent clipboard registers for COSMIC/Wayland.

**Version:** 0.2.0-dev.1 (development checkpoint; not released)
**Status:** pre-1.0.
**License:** GNU Affero General Public License v3.0 only (`AGPL-3.0-only`).

ClipReg stores multi-MIME clipboard objects in named persistent registers while
preserving the user's normal clipboard around grab/paste transactions. It uses
Wayland ext-data-control for clipboard ownership/transfer, COSMIC toplevel info
for active-app profiles, and a restricted uinput virtual keyboard for native
Copy/Paste accelerators.

## Core commands

- `clipreg grab NAME` — native Copy current app selection into a register, then restore clipboard.
- `clipreg save NAME` — save current clipboard directly.
- `clipreg primary NAME` — save current Wayland primary selection.
- `clipreg paste NAME` — native-paste a register, then restore clipboard.
- `clipreg copy NAME` — leave a register on the real clipboard.
- `clipreg show|clear NAME`, `clipreg list`, `clipreg app`, `clipreg doctor`.
- `clipreg autostart on|off|status`.

Default COSMIC maps provide 1..9,0 and F1..F12 register sets. The map is
customizable in `~/.config/clipreg/keymap.toml` and installed with
`clipreg-keybinds`.

## Build/test

```bash
./test.sh
make clean selftest
```

A real install additionally requires `libwayland-dev` and `wayland-scanner`:

```bash
./scripts/install.sh
clipreg doctor
clipreg-keybinds
```

## Runtime data

- Config: `~/.config/clipreg/`
- Registers: `~/.local/share/clipreg/registers/`
- Runtime socket/recovery: `$XDG_RUNTIME_DIR/clipreg/`
- User service: `clipreg.service`

ClipReg refuses known password-manager secret markers from persistent register
storage. v1 CopyQ-backed registers can be migrated read-only by the installer.
