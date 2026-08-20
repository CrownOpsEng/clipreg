#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/data/clipreg/registers"

cat > "$TMP/bin/copyq" <<'PY'
#!/usr/bin/env python3
import base64, sys
if len(sys.argv) >= 2 and sys.argv[1] == '--start-server':
    raise SystemExit(0)
if len(sys.argv) >= 3 and sys.argv[1:3] == ['eval', '-']:
    sys.stdin.read()
    b64 = lambda b: base64.b64encode(b).decode()
    print('REGISTER\t' + b64(b'1') + '\t0')
    print('MIME\t' + b64(b'text/plain') + '\t' + b64(b'hello\x00world'))
    print('MIME\t' + b64(b'text/html') + '\t' + b64(b'<b>hello</b>'))
    print('END')
    print('REGISTER\t' + b64(b'2') + '\t1')
    print('END')
    print('REGISTER\t' + b64(b'3') + '\t0')
    print('MIME\t' + b64(b'text/plain') + '\t' + b64(b'do-not-overwrite'))
    print('END')
    raise SystemExit(0)
raise SystemExit(2)
PY
chmod +x "$TMP/bin/copyq"
printf 'existing' > "$TMP/data/clipreg/registers/3.clipreg"

PATH="$TMP/bin:$PATH" XDG_DATA_HOME="$TMP/data" \
    "$ROOT/scripts/migrate-v1.py" > "$TMP/out"

grep -q 'v1 CopyQ register -> v2: 1' "$TMP/out"
grep -q 'skip secret-marked v1 register: 2' "$TMP/out"
grep -q 'keep existing v2 register: 3' "$TMP/out"

python3 - "$TMP/data" <<'PY'
from pathlib import Path
import struct, sys
root = Path(sys.argv[1]) / 'clipreg'
p = root / 'registers/1.clipreg'
b = p.read_bytes()
assert b[:8] == b'CLPRG002'
ver, count, created, source, app_len = struct.unpack_from('<IIQII', b, 8)
assert ver == 2 and count == 2 and source == 5 and created > 0
off = 8 + 24
assert b[off:off+app_len] == b'copyq-v1'
off += app_len
items = {}
for _ in range(count):
    name_len, data_len = struct.unpack_from('<IQ', b, off)
    off += 12
    name = b[off:off+name_len].decode('utf-8')
    off += name_len
    data = b[off:off+data_len]
    off += data_len
    items[name] = data
assert off == len(b)
assert items['text/plain'] == b'hello\x00world'
assert items['text/html'] == b'<b>hello</b>'
assert not (root / 'registers/2.clipreg').exists()
assert (root / 'registers/3.clipreg').read_bytes() == b'existing'
assert (root / '.v1-copyq-migration-complete').exists()
PY

PATH="$TMP/bin:$PATH" XDG_DATA_HOME="$TMP/data" \
    "$ROOT/scripts/migrate-v1.py" | grep -q 'already completed'

echo 'v1 migration tests: PASS'
