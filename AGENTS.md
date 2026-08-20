# ClipReg agent router

## Mission

Build a small, trustworthy COSMIC/Wayland clipboard-register service whose correctness is explicit at protocol, transaction, persistence, deployment, and repository boundaries.

## Route by concern

Read the owner before changing the concern:

- Runtime behavior/invariants → `docs/architecture.md`.
- Repository, Git, PR, CI, generated-file policy → `docs/foundation.md`.
- Install/upgrade/autostart/migration → `docs/deployment.md` and `scripts/AGENTS.md`.
- Automated/live acceptance → `docs/qualification.md`.
- Version/tag/release semantics → `docs/release.md`.
- GitHub metadata/workflows → `.github/AGENTS.md`.
- Native engine work → `src/AGENTS.md`.
- Operator/developer commands → `Makefile` (`make help`).

If authoritative surfaces disagree, report drift instead of silently choosing one.

## Hard rules

- Current explicit user instruction has highest precedence.
- Treat consequential bugs as violated invariants; root-cause the failure class and add a durable regression.
- A service restart is containment, never proof of correctness.
- Do not track generated build/protocol output.
- Do not bypass protected `main`; normal work uses a topic branch + PR.
- Do not cut beta/RC/final tags merely to obtain CI or package a single fix; follow `docs/release.md`.
- Keep adapters and scoped `AGENTS.md` files as routers to authority rather than duplicate policy.
