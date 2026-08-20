# Repository foundation

## Purpose

This document governs how ClipReg changes are proposed, reviewed, validated, and integrated. Runtime meaning belongs in `docs/architecture.md`; deployment behavior belongs in `docs/deployment.md`; promotion/version semantics belong in `docs/release.md`; acceptance evidence belongs in `docs/qualification.md` and pull requests.

The repository should make correct work reproducible and incorrect assumptions visible without growing process or tooling that has not earned its cost.

## Authority map

One owner per concern:

- Runtime contracts and invariants: `docs/architecture.md`.
- Repository, Git, PR, CI, and generated-file policy: this document.
- Installation, upgrade, autostart, and local machine mutation: `docs/deployment.md`.
- Test/qualification expectations: `docs/qualification.md`.
- Versioning, prerelease promotion, tags, and GitHub releases: `docs/release.md`.
- User-facing command/config behavior: `README.md`, subordinate to the documents above.
- Operator/developer command catalog: `Makefile`; run `make help`.
- Active defects, investigations, and feature proposals: GitHub issues and pull requests.

If authoritative surfaces disagree, treat that as drift. Do not silently choose one and rewrite the others.

## Durable target state vs records

Target-state code, configuration, tests, and documentation describe current accepted behavior in neutral present tense. Chronology, experiments, rejected alternatives, incident narratives, and review rationale belong in issues, pull requests, release notes, or commits when useful.

The changelog is intentionally a historical record rather than target-state documentation.

## Working principles

- Recover the violated invariant and root cause before adding a symptom-specific workaround.
- Consequential bug fixes add a regression at the lowest stable executable layer that can protect the failure class.
- For protocol/lifecycle failures, instrument the smallest material boundary before making architectural conclusions.
- A service restart is containment, not a fix.
- Keep diagnostics non-mutating unless their command name and help explicitly state otherwise.
- Generated code/build output is disposable and never tracked.
- Prefer the strongest existing primitive before adding a service, daemon, dependency, compatibility layer, or new config surface.
- Remove a capability if it stops earning its complexity.

## Git and pull-request workflow

A branch is a temporary work reference; the pull request is the durable review record; `main` is the durable integrated development history.

- Begin non-trivial work from current `main` on `agent/<issue>-<descriptive-scope>` when an issue exists, otherwise `agent/<descriptive-scope>`.
- Keep one PR to one coherent review unit. Independently useful concerns belong in follow-up PRs rather than expanding scope by habit.
- Keep reproduction, red-team, instrumentation, and corrective iterations for the same failure class on the same PR. A draft PR may intentionally be red while it proves a regression.
- Make commits semantic checkpoints rather than transport artifacts.
- Treat pushes to an open PR as CI checkpoints: run inexpensive local checks first, then push when remote proof/review is useful.
- Ordinary PRs squash-merge. Exploratory branch commits remain available in the PR without polluting `main`.
- Delete merged topic branches unless an active follow-up has a concrete reason to keep the ref.
- Never rewrite `main` after genesis. Tags and published releases are immutable.

## Public-repository merge policy

`main` is protected after genesis:

- changes require a pull request;
- canonical CI must pass and the branch must be current with `main`;
- review conversations must be resolved;
- zero external approvals are required because the project currently has one maintainer and self-authored PRs cannot satisfy a one-reviewer rule;
- administrators are subject to the same protection;
- force pushes and branch deletion are disabled;
- linear history is required;
- repository merge mode is squash-only for ordinary PR integration.

Adding required human approvals becomes appropriate when the maintainer set grows enough that the rule no longer deadlocks ordinary work.

## Issues

Issues are durable problem/decision anchors, not mandatory ceremony for every typo.

Create or reuse an issue when work involves a consequential bug, architecture/reliability question, externally visible behavior change, security concern, or release blocker. Bug reports should distinguish observed symptom from violated invariant and must not include secret clipboard payloads.

Release blockers use the `priority:blocker` label and the relevant release milestone. Closing an issue requires evidence that its acceptance conditions are met, not merely that a patch exists.

## CI

`.github/workflows/ci.yml`, top-level name `CI`, is the only permanent workflow.

- CI permissions remain read-only unless a separately approved durable requirement earns more privilege.
- Checkout credentials remain disabled (`persist-credentials: false`).
- `make check` is canonical validation.
- A temporary helper workflow is allowed only for a concrete infrastructure limitation and must be named `.github/workflows/tmp-<purpose>.yml`; repository validation intentionally fails while one remains in the tree.

## Generated and local state

Never commit:

- `build/` or `dist/`;
- generated Wayland C/headers;
- object files/binaries;
- runtime register data, recovery journals, logs, or user config;
- locally downloaded deployment/source archives.

The Makefile and installer regenerate what is disposable.

## Remote safety

Repository-safe checks do not mutate GitHub or the local workstation.

Remote administration scripts are dry-run/plan first and require an explicit apply flag. Installation is necessarily machine-mutating and is named/documented as such. GitHub configuration changes are centralized in `scripts/configure-github.sh`; release publication is centralized in `scripts/release.sh`.

## Complexity rule

Promote the smallest new capability only when an observed need exists and current primitives cannot solve it cleanly without compensating complexity. Do not add a framework, package manager, second daemon, telemetry pipeline, plugin system, or compatibility abstraction merely because it could be useful later.
