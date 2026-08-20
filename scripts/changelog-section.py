#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
import re, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
version=sys.argv[1].removeprefix('v') if len(sys.argv)>1 else (root/'VERSION').read_text().strip()
text=(root/'CHANGELOG.md').read_text()
pat=re.compile(rf'^## v{re.escape(version)}\s*$\n(.*?)(?=^## |\Z)', re.M|re.S)
m=pat.search(text)
if not m:
    raise SystemExit(f'No CHANGELOG section for v{version}')
print(m.group(1).strip())
