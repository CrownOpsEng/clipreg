# Runtime architecture

## Mission

ClipReg provides persistent named clipboard registers on COSMIC/Wayland while preserving the user's ordinary clipboard across grab/paste transactions wherever the Wayland/application contracts permit it.

## Supported boundary

The current supported environment is Pop!_OS 24.04 with COSMIC/Wayland. XWayland applications are supported only through their compositor/application-visible clipboard behavior. Other compositors are not a current compatibility promise even when they expose similar protocols.

## Core components

- `ext-data-control-v1`: observe, receive, own, and serve regular/primary selections.
- COSMIC toplevel information: identify the activated application for accelerator policy.
- `/dev/uinput`: emit a deliberately restricted set of native Copy/Paste key chords.
- atomic register files: persist multi-MIME register objects independently of CopyQ.
- runtime transaction journal: recover interrupted clipboard transactions conservatively.
- one user daemon: owns Wayland state, transaction serialization, input injection, and the client socket.

CopyQ is optional history integration. It is not a correctness or persistence backend in the native line.

## Runtime invariants

1. A failed `grab` never overwrites the prior register.
2. A failed `paste` attempts to restore the clipboard snapshot captured before staging.
3. Recovery never overwrites clipboard data that can be proven newer than the interrupted transaction.
4. Every bounded command returns the daemon to a command-ready state or fails in a way that makes the service failure observable.
5. Clipboard consumers may close transfer pipes early; `EPIPE` is an ordinary transfer outcome and must never terminate the daemon.
6. ClipReg must not deadlock by requesting a selection from itself; self-owned selections are cloned from held MIME data.
7. Known secret/password-manager markers are not persisted as registers.
8. Register replacement is atomic and durable at the directory level.
9. One paste command emits exactly one selected paste accelerator.
10. Unknown applications are never given a potentially destructive generic Copy chord merely to guess at compatibility.

## Application-copy boundary

Wayland does not provide a universal semantic API for "serialize the current arbitrary selection." Rich capture therefore uses the active application's Copy behavior. A generic Copy probe is attempted only where the application profile declares the ordinary `copy` strategy. `primary-first` terminal profiles use an affine primary selection when available and otherwise go directly to their explicit profile Copy accelerator such as terminal `Ctrl+Shift+C`; they do not receive the generic probe first.

Primary selection is an explicit alternate capture source, not a silent fallback for a failed application Copy.

## Failure philosophy

Timeouts bound waiting; they are not used as proof that clipboard work completed. Where the protocol exposes observable state transitions or data requests, those events are the synchronization authority.

A daemon that becomes permanently non-responsive after a bounded transaction violates the command-ready invariant even if systemd still reports the process as running. That class of failure is a release blocker until root-caused and regression-protected.
