#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
TARGET="$TMP/custom"
KEYMAP="$TMP/keymap.toml"
PREF="$TMP/keybind-mode"
cp "$ROOT/config/keymap.toml" "$KEYMAP"
cat > "$TARGET" <<'RON'
{
    // UNRELATED
    (
        modifiers: [
            Super,
            Alt,
        ],
        key: "Q",
    ): Spawn("unrelated-command"),
}
RON

run() {
    CLIPREG_KEYMAP="$KEYMAP" CLIPREG_KEYBIND_PREF="$PREF" CLIPREG_COSMIC_SHORTCUT_FILE="$TARGET" "$ROOT/scripts/clipreg-keybinds" "$@"
}

run check >/dev/null
run digits >/dev/null
[[ "$(grep -c 'clipreg --notify' "$TARGET")" -eq 30 ]]
grep -q 'unrelated-command' "$TARGET"
grep -q 'clipreg --notify grab 1' "$TARGET"
grep -q 'clipreg --notify save 0' "$TARGET"
grep -q 'clipreg --notify paste 9' "$TARGET"

[[ "$(cat "$PREF")" == "digits" ]]
# Default/ask must reapply the saved mode without prompting.
run ask >/dev/null
[[ "$(grep -c 'clipreg --notify' "$TARGET")" -eq 30 ]]

before="$(sha256sum "$TARGET")"
run skip >/dev/null
after="$(sha256sum "$TARGET")"
[[ "$before" == "$after" ]]

run functions >/dev/null
[[ "$(grep -c 'clipreg --notify' "$TARGET")" -eq 36 ]]
grep -q 'clipreg --notify grab F1' "$TARGET"
grep -q 'clipreg --notify paste F12' "$TARGET"
! grep -q 'clipreg --notify grab 1"' "$TARGET"

run both >/dev/null
[[ "$(grep -c 'clipreg --notify' "$TARGET")" -eq 66 ]]

cat >> "$KEYMAP" <<'TOML'
[[binding]]
set = "custom"
register = "email"
enabled = true
grab = { key = "E", modifiers = ["Super", "Alt", "Shift"] }
save = { key = "E", modifiers = ["Super", "Ctrl", "Alt", "Shift"] }
paste = { key = "E", modifiers = ["Ctrl", "Alt", "Shift"] }
TOML
run set:custom >/dev/null
[[ "$(grep -c 'clipreg --notify' "$TARGET")" -eq 3 ]]
grep -q 'clipreg --notify grab email' "$TARGET"
grep -q 'clipreg --notify save email' "$TARGET"
grep -q 'clipreg --notify paste email' "$TARGET"

run remove >/dev/null
[[ ! -e "$PREF" ]]
! grep -q 'CLIPREG-START' "$TARGET"
grep -q 'unrelated-command' "$TARGET"


# v1 migration: an existing managed save/paste block with no v2 grab actions
# should infer the previous established set instead of prompting.
rm -f "$PREF"
cat > "$TARGET" <<'RON'
{
    // CLIPREG-START
    (
        modifiers: [
            Super,
            Shift,
        ],
        key: "1",
    ): Spawn("$HOME/.local/bin/clipreg save 1"),
    (
        modifiers: [
            Ctrl,
            Shift,
        ],
        key: "1",
    ): Spawn("$HOME/.local/bin/clipreg paste 1"),
    // CLIPREG-END
}
RON
run ask >/dev/null
[[ "$(cat "$PREF")" == "digits" ]]
grep -q 'clipreg --notify grab 1' "$TARGET"
[[ "$(grep -c 'clipreg --notify' "$TARGET")" -eq 30 ]]

# A v2 block without a preference is not guessed from; `skip` must remain a
# genuine no-op even in that state.
rm -f "$PREF"
before="$(sha256sum "$TARGET")"
run skip >/dev/null
after="$(sha256sum "$TARGET")"
[[ "$before" == "$after" ]]

# Existing custom collision must fail without modifying the file.
cat > "$TARGET" <<'RON'
{
    (
        modifiers: [
            Super,
            Shift,
        ],
        key: "1",
    ): Spawn("something-else"),
}
RON
before="$(sha256sum "$TARGET")"
if run digits >"$TMP/out" 2>"$TMP/err"; then
    echo "expected custom shortcut collision to fail" >&2
    exit 1
fi
grep -q 'conflicts with an existing COSMIC custom shortcut' "$TMP/err"
after="$(sha256sum "$TARGET")"
[[ "$before" == "$after" ]]

echo "keybinding tests: PASS"

# Alternate presets may intentionally reuse the same physical chords. They are
# valid independently and rejected only if the user selects both at once.
cat >> "$KEYMAP" <<'TOML'
[[binding]]
set = "preset-a"
register = "alpha"
enabled = true
grab = { key = "A", modifiers = ["Super", "Alt"] }

[[binding]]
set = "preset-b"
register = "beta"
enabled = true
grab = { key = "A", modifiers = ["Super", "Alt"] }
TOML

# `check` validates each preset independently, so intentional cross-preset reuse is OK.
run check >/dev/null
run set:preset-a >/dev/null
grep -q 'clipreg --notify grab alpha' "$TARGET"
run set:preset-b >/dev/null
grep -q 'clipreg --notify grab beta' "$TARGET"

before="$(sha256sum "$TARGET")"
if run set:preset-a,preset-b >"$TMP/out" 2>"$TMP/err"; then
    echo "expected colliding selected presets to fail" >&2
    exit 1
fi
grep -q 'duplicate shortcut in selected set combination' "$TMP/err"
after="$(sha256sum "$TARGET")"
[[ "$before" == "$after" ]]

if run all >"$TMP/out" 2>"$TMP/err"; then
    echo "expected all to reject colliding alternate presets" >&2
    exit 1
fi

run list >"$TMP/list"
grep -q $'^digits\t10\t1 2 3 4 5 6 7 8 9 0$' "$TMP/list"
grep -q $'^functions\t12\tF1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12$' "$TMP/list"
grep -q $'^preset-a\t1\talpha$' "$TMP/list"

# Restore a non-conflicting target before checking backup persistence.
cat > "$TARGET" <<'RON'
{
    // UNRELATED
    (
        modifiers: [
            Super,
            Alt,
        ],
        key: "Q",
    ): Spawn("unrelated-command"),
}
RON

# First mutation creates a one-time backup of the pre-ClipReg-v2 shortcut file.
# Later updates must not overwrite that recovery point.
BACKUP="$TARGET.pre-clipreg-v2"
[[ -f "$BACKUP" ]]
backup_hash="$(sha256sum "$BACKUP")"
run digits >/dev/null
[[ "$backup_hash" == "$(sha256sum "$BACKUP")" ]]

echo "extended keybinding tests: PASS"
