# Deployment contract

## Entry points

- Source checkout/package: `./deploy.sh`.
- Published release wrapper: `deploy-clipreg.sh` from the release deployment package.
- Low-level install planner/executor: `scripts/install.sh`.

Run `make help` for repository commands.

## Rebuild decision

The installer records under `~/.local/state/clipreg/build-state`:

- release-line `version_base` from `VERSION`;
- exact `build_version` (Git commit metadata is appended for untagged working-tree builds);
- source fingerprint of native build inputs;
- installed binary SHA-256.

A rebuild is required when the binary is absent, exact build identity changed, source fingerprint changed, installed binary hash changed, saved state is incomplete/stale, or `--rebuild` is explicit. Otherwise the existing binary is reused while integration files are reconciled.

`./deploy.sh --plan` is non-mutating and reports the decision/reasons.

## User state preservation

Normal upgrades preserve existing:

- register files;
- user key map and application profiles;
- chosen shortcut set;
- enabled/disabled autostart state.

`--reset-config` is the explicit config-replacement path.

## Autostart

The first native install enables `clipreg.service` for the COSMIC session. Later installs preserve that choice unless `--autostart` or `--no-autostart` is explicit.

`clipreg autostart on|off|status` is the user-facing control. Startup-disabled ClipReg may still be started on demand by a command; that must not silently re-enable login startup.

## Privileged machine changes

Installation may:

- install missing APT build/runtime dependencies;
- install a udev rule granting session access to `/dev/uinput`;
- load/configure the `uinput` module;
- install/reload the user systemd unit.

Do not add the user to the broad Linux `input` group merely to make ClipReg work.

## v0.1 migration

The native installer may read the old CopyQ `Clipboard Registers` tab and migrate compatible MIME objects into standalone register files. Migration is idempotent, never overwrites an existing native register, refuses known secret-marked items, does not put migrated data onto the live clipboard, and leaves the CopyQ source tab intact.
