# Changelog

ClipReg uses Semantic Versioning during initial development. Published tags/releases are immutable; prerelease identifiers represent promotion states rather than individual pull requests.

## Unreleased

- v0.2 stabilization continues through protected-main pull requests.
- A known daemon-liveness defect can leave the service non-responsive until restart after repeated operations; this blocks release-candidate promotion.

## v0.2.0-beta.1

First public beta of the native transactional architecture.

- Replace the CopyQ-backed shell engine with a native Wayland daemon.
- Use `ext-data-control-v1` for multi-MIME clipboard ownership and transfer.
- Detect active COSMIC applications for safer Copy/Paste accelerator profiles.
- Add restricted uinput injection, standalone atomic register storage, runtime crash recovery, configurable register/key maps, login autostart, and read-only v0.1 CopyQ-register migration.
- Keep CopyQ optional as a clipboard-history companion rather than a backend.
- Ignore `SIGPIPE` so clipboard consumers closing a MIME transfer early return `EPIPE` instead of terminating the daemon, with a regression self-test for that failure class.
- Add source/build fingerprints so deployment rebuilds only when required and verifies the installed binary before reuse.
- Establish public repository governance, CI, issue/PR standards, router-based agent guidance, and prerelease promotion gates.

This beta is intentionally not an RC because a repeated-operation daemon-liveness blocker remains unresolved.

## v0.1.0

Initial functional prototype.

- Persistent `0..9` and `F1..F12` registers stored in a dedicated CopyQ tab.
- Plain-text recall through `wtype` without changing the active clipboard.
- Rich clipboard recall through temporary stage/paste/restore transactions.
- Managed COSMIC digit/function keybinding presets and secret-marker refusal.
