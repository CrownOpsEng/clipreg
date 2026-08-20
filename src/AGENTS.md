# Native-engine agent router

Before changing `src/`, read:

1. `../docs/architecture.md` — runtime authority and invariants.
2. `../docs/qualification.md` — required executable/live proof.
3. `../docs/foundation.md` — root-cause/regression and PR rules.

Native fixes should close a failure class at the smallest stable layer. Preserve bounded transactions, self-owned selection handling, consumer disconnect tolerance, conservative recovery, and explicit app-input policy. Do not add protocol/runtime machinery without an observed need.
