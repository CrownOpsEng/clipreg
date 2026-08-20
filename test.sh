#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

printf '[1/8] shell/python syntax\n'
while IFS= read -r -d '' script; do bash -n "$script"; done < <(find "$ROOT" -maxdepth 2 -type f -name '*.sh' -o -type f -path '*/scripts/clipreg-keybinds' -print0)
python3 -m py_compile "$ROOT/scripts/migrate-v1.py"

printf '[2/8] config/protocol syntax\n'
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys, tomllib, xml.etree.ElementTree as ET
root=Path(sys.argv[1])
with (root/'config/keymap.toml').open('rb') as f: data=tomllib.load(f)
assert data.get('version') == 1
assert len(data.get('binding', [])) >= 22
for p in (root/'protocol').glob('*.xml'): ET.parse(p)
assert (root/'VERSION').read_text().strip() == '0.2.0-dev.1'
print('config/protocol syntax: PASS')
PY

printf '[3/8] keybinding generator\n'
"$ROOT/tests/test-keybinds.sh"
printf '[4/8] v1 CopyQ register migration\n'
"$ROOT/tests/test-migration.sh"
printf '[5/8] strict native compile (offline protocol stubs)\n'
CC="${CC:-cc}"
COMMON=(-std=c11 -g -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Werror -I"$ROOT/tests/stubs")
"$CC" "${COMMON[@]}" -O2 "$ROOT/src/clipreg.c" "$ROOT/tests/stubs/stub_impl.c" -o "$TMP/clipreg"
printf '[6/8] binary self-test\n'
"$TMP/clipreg" --selftest
printf '[7/8] client/autostart behavior\n'
CLIPREG_TEST_BINARY="$TMP/clipreg" "$ROOT/tests/test-client.sh"
printf '[8/8] sanitizers (when available)\n'
if "$CC" "${COMMON[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer "$ROOT/src/clipreg.c" "$ROOT/tests/stubs/stub_impl.c" -o "$TMP/clipreg-asan" >/dev/null 2>"$TMP/sanitize.err"; then
  ASAN_OPTIONS=detect_leaks=1 "$TMP/clipreg-asan" --selftest
else
  echo 'sanitizers unavailable; skipped'
fi
echo 'ClipReg standalone offline test suite: PASS'
