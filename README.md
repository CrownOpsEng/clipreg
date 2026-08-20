# ClipReg v0.1.0

Persistent clipboard registers for Pop!_OS COSMIC/Wayland.

This is the original pre-native prototype. Registers are persisted inside a
CopyQ tab and plain text is injected with `wtype`; rich content uses a temporary
clipboard swap followed by restoration.

## Commands

```text
clipreg save KEY
clipreg paste KEY
clipreg type KEY
clipreg copy KEY
clipreg show KEY
clipreg clear KEY
clipreg list
clipreg doctor
```

`KEY` may be `0..9` or `F1..F12`.

Install with:

```bash
./scripts/install.sh
clipreg-keybinds
```

This release is intentionally a prototype; its CopyQ-backed persistence and command behavior are not a stable API.

## License

GNU Affero General Public License v3.0 only (`AGPL-3.0-only`).
