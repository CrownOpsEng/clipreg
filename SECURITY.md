# Security policy

ClipReg interacts with clipboard contents and can emit a restricted set of virtual keyboard inputs, so security reports deserve careful handling.

For a vulnerability that would expose secrets, bypass intended input restrictions, corrupt arbitrary files, or materially weaken local privilege boundaries, use GitHub's private vulnerability reporting surface for this repository when available. Do not post exploit details or real secret clipboard data in a public issue.

For non-sensitive hardening concerns, open a normal issue and clearly identify the affected boundary.

Supported security-fix line: the current v0.x development line. Pre-1.0 interfaces may change while correcting a security boundary.
