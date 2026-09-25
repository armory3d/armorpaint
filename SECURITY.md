# Security Policy

## Supported versions

The `main` branch tracks the latest development snapshot. There are no long-term
supported releases — users are encouraged to build from or update to `main`.

| Version | Supported |
| ------- | --------- |
| main (git) | yes |

## Reporting a vulnerability

Please **do not** open a public issue for security vulnerabilities.

Report privately to the maintainers:

- Upstream project: https://github.com/armory3d/armorpaint/security/advisories
- Forum (private message): https://forums.armorpaint.org

Please include:

- The affected component (paint, base/iron, plugins, WASM, ...)
- Steps to reproduce and impact
- Suggested fix, if known

You should receive an acknowledgment within 7 days. We will coordinate on a
fix, and credit the reporter (unless anonymity is requested) once a fix is
released.

## Scope

- C sources under `base/` and `paint/` (native engine)
- Plugins under `paint/plugins/`
- The WASM build (`--target wasm`)

Reports about user data, AI features, or third-party dependencies will be
triage as needed.