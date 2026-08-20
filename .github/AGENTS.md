# GitHub-scoped agent router

For `.github/` changes, read:

1. `../docs/foundation.md` — PR/CI/branch-governance authority.
2. `../docs/release.md` — release/promotion semantics.
3. `../docs/qualification.md` — canonical validation evidence.

`workflows/ci.yml` (`name: CI`) is the only permanent workflow. Keep permanent CI read-only and checkout credentials disabled. Temporary helper workflows must follow the `tmp-<purpose>.yml` convention and be removed before acceptance. Do not duplicate repository policy into workflow comments or this router.
