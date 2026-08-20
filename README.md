# ClipReg

Transactional persistent clipboard registers for COSMIC/Wayland.

**Current version:** 0.2.0-dev.2 (development checkpoint; not released)
**Status:** pre-1.0; interfaces may still change between minor releases.
**License:** GNU Affero General Public License v3.0 only (`AGPL-3.0-only`).

ClipReg lets you capture selected application content or the current clipboard
into persistent named registers, then native-paste those registers later while
restoring the user's normal clipboard. Registers preserve all captured MIME
representations rather than reducing content to plain text.

## Default interaction

For the established digit set (`1..9,0`):

- `Super+Shift+N` — grab the current application selection into register `N`.
- `Super+Ctrl+Shift+N` — save the existing clipboard into register `N`.
- `Ctrl+Shift+N` — native-paste register `N` and restore the prior clipboard.

An equivalent `F1..F12` preset is included. The complete map is customizable in
`~/.config/clipreg/keymap.toml`, including named registers and custom sets.

## Architecture

- Wayland `ext-data-control-v1` for clipboard/primary-selection ownership.
- COSMIC toplevel information for active-application profiles.
- Restricted `/dev/uinput` virtual keyboard for native Copy/Paste accelerators.
- Atomic multi-MIME register files under `~/.local/share/clipreg/registers/`.
- Runtime transaction/recovery state under `$XDG_RUNTIME_DIR/clipreg/`.
- `clipreg.service`, scoped to the COSMIC session, for fast startup/recovery.
- CopyQ is optional; v0.1.x CopyQ-backed registers are migrated read-only.

## Deploy from a source checkout/package

```bash
./test.sh
./deploy.sh
```

The first deployment enables startup by default and asks which shortcut preset
to install. Later deployments preserve your startup and shortcut choices.

Force a rebuild only when desired:

```bash
./deploy.sh --rebuild
```

The installer automatically rebuilds when the source fingerprint, version, or
installed binary checksum differs from its saved build state. Otherwise it
reuses the existing native binary while refreshing integration files.

Useful variants:

```bash
./deploy.sh --keybinds digits
./deploy.sh --keybinds functions
./deploy.sh --keybinds both
./deploy.sh --no-autostart
./deploy.sh --reset-config --rebuild
```

## Commands

```text
clipreg grab NAME       native Copy selection -> register
clipreg save NAME       current clipboard -> register
clipreg primary NAME    primary selection -> register
clipreg paste NAME      register -> native Paste; restore clipboard
clipreg copy NAME       register -> clipboard and leave it there
clipreg show NAME
clipreg clear NAME
clipreg list
clipreg app              active app + selected input profile
clipreg doctor
clipreg autostart on|off|status
```

## Build and tests

Offline suite:

```bash
./test.sh
```

Real Wayland development build:

```bash
sudo apt install build-essential pkg-config libwayland-dev libwayland-bin
make clean selftest
```

The CI workflow runs both on Ubuntu 24.04.

## Runtime paths

- Config: `~/.config/clipreg/`
- Registers: `~/.local/share/clipreg/registers/`
- Install/build state: `~/.local/state/clipreg/`
- Runtime socket/recovery: `$XDG_RUNTIME_DIR/clipreg/`
- User service: `clipreg.service`

ClipReg refuses known password-manager secret markers from persistent register
storage. See `CHANGELOG.md` for the pre-1.0 history.
