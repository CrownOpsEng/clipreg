# ClipReg

Transactional persistent clipboard registers for Pop!_OS COSMIC/Wayland.

ClipReg captures selected application content or the current clipboard into persistent named registers, then native-pastes those registers later while restoring the user's ordinary clipboard. Registers preserve captured MIME representations rather than reducing content to plain text.

**Status:** pre-1.0 and currently in the v0.2 beta line. The current native build is intentionally **not** called a release candidate while a daemon-liveness blocker remains open.

**License:** GNU Affero General Public License v3.0 only (`AGPL-3.0-only`).

## Default interaction

For the established digit set (`1..9,0`):

- `Super+Shift+N` — grab the current application selection into register `N`.
- `Super+Ctrl+Shift+N` — save the existing clipboard into register `N`.
- `Ctrl+Shift+N` — native-paste register `N` and restore the prior clipboard.

An equivalent `F1..F12` preset is included. The complete map is customizable in `~/.config/clipreg/keymap.toml`, including named registers and custom sets.

## Start here

```bash
make help
make check-fast
./deploy.sh --plan
./deploy.sh
```

The first native deployment enables startup by default and asks which shortcut preset to install. Later deployments preserve startup, register data, user configuration, and shortcut choices unless an explicit reset/change is requested.

Useful commands:

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

## Project authority

- Runtime architecture/invariants: `docs/architecture.md`
- Repository/PR/CI conventions: `docs/foundation.md`
- Deployment contract: `docs/deployment.md`
- Qualification: `docs/qualification.md`
- Version/release policy: `docs/release.md`
- Agent routing: `AGENTS.md`

`VERSION` owns the base SemVer. Untagged Git builds append commit metadata automatically, so test builds are uniquely identifiable without cutting a new prerelease tag for every PR.

## Runtime paths

- Config: `~/.config/clipreg/`
- Registers: `~/.local/share/clipreg/registers/`
- Install/build state: `~/.local/state/clipreg/`
- Runtime socket/recovery: `$XDG_RUNTIME_DIR/clipreg/`
- User service: `clipreg.service`

ClipReg refuses known password-manager secret markers from persistent register storage.

## Contributing

See `CONTRIBUTING.md`. Normal non-trivial changes use an issue when useful, a topic branch, a pull request, and green canonical CI. `main` is protected after repository genesis.
