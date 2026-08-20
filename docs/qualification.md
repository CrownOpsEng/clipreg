# Qualification

## Canonical automated proof

- `make check-fast`: offline/static suite using protocol-accurate stubs, serialization/regression tests, migration tests, keymap tests, install-plan tests, and sanitizers when available.
- `make check-native`: real `wayland-scanner` + installed `libwayland-client` build and native self-test.
- `make check`: canonical full proof; CI runs this on Ubuntu 24.04.

Automated checks must leave tracked files unchanged.

## Live COSMIC qualification

Protocol correctness is not sufficient proof of desktop behavior. A promotion checkpoint records live evidence on the supported COSMIC environment for at least:

- repeated plain-text grab/paste while preserving an unrelated clipboard;
- rich browser/office MIME capture and paste;
- selected file capture/paste where supported by the application;
- COSMIC Terminal capture without delivering SIGINT;
- failed Copy leaving the destination register unchanged;
- failed Paste restoring the prior clipboard;
- startup/logout/login behavior;
- v0.1 register migration when applicable;
- repeated operations while observing service liveness/restart count.

## Liveness acceptance

A bounded operation must not leave the daemon alive-but-permanently-unusable. Restarting the service is not acceptance.

For the current v0.2 stabilization line, the known repeated-operation liveness defect blocks release-candidate promotion. Its corrective PR should include instrumentation sufficient to identify where progress stops and a durable regression at the lowest practical layer.

## Stress evidence

When qualifying a liveness fix, repeat the candidate enough to expose lifecycle failures rather than proving only a single happy path. Record the application(s), operation count, service `NRestarts`, command responsiveness after the loop, and any relevant diagnostics in the PR/issue. Promotion-only instrumentation may be removed after a smaller durable regression protects the root cause.
