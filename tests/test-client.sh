#!/usr/bin/env bash
set -euo pipefail

BIN="${CLIPREG_TEST_BINARY:?set CLIPREG_TEST_BINARY to the compiled test binary}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin"
LOG="$TMP/systemctl.log"

cat > "$TMP/bin/systemctl" <<'SYSTEMCTL'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "${CLIPREG_TEST_SYSTEMCTL_LOG:?}"
case "$*" in
    "--user is-enabled clipreg.service") echo enabled ;;
    "--user is-active clipreg.service") echo active ;;
    "--user enable --now clipreg.service") echo enabled ;;
    "--user disable --now clipreg.service") echo disabled ;;
    "--user start clipreg.service") ;;
    *) echo "unexpected systemctl invocation: $*" >&2; exit 2 ;;
esac
SYSTEMCTL
chmod +x "$TMP/bin/systemctl"

export CLIPREG_TEST_SYSTEMCTL_LOG="$LOG"
export PATH="$TMP/bin:/usr/bin:/bin"

[[ "$($BIN --version)" == "0.2.0-dev.1" ]]
"$BIN" autostart on > "$TMP/on"
grep -q 'autostart: on' "$TMP/on"
grep -q '^--user enable --now clipreg.service$' "$LOG"

"$BIN" autostart off > "$TMP/off"
grep -q 'autostart: off' "$TMP/off"
grep -q '^--user disable --now clipreg.service$' "$LOG"

"$BIN" autostart status > "$TMP/status"
grep -q '^autostart=on$' "$TMP/status"
grep -q '^service=active$' "$TMP/status"

if "$BIN" autostart nonsense >"$TMP/out" 2>"$TMP/err"; then
    echo "expected invalid autostart mode to fail" >&2
    exit 1
fi
grep -q 'unknown autostart mode' "$TMP/err"

echo "client/autostart tests: PASS"
