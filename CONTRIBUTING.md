# Contributing

Thank you for improving ClipReg.

Before substantial work:

1. Read `AGENTS.md` for the authority router.
2. Read `docs/foundation.md` for repository/PR policy.
3. Read the concern owner (`docs/architecture.md`, `docs/deployment.md`, `docs/qualification.md`, or `docs/release.md`).
4. Run `make help` for the supported command surface.

Use an issue for consequential bugs, architecture/reliability questions, externally visible behavior changes, security concerns, and release blockers. Small obvious corrections do not need ceremony.

Normal work branches from current `main` and lands by pull request. The PR template captures why, what, validation, and open/deferred work. Bug fixes should state the violated invariant/root cause and add a durable regression where practical.

Run `make check-fast` before ordinary pushes and `make check` before asking to merge when the native Wayland build dependencies are available.

Do not include secrets, private clipboard payloads, credentials, or unrelated workstation configuration in issues, tests, or commits.
