# Contributing to ArmorPaint

Thanks for taking the time to contribute! Please read the whole guide before opening an issue or a pull request.

## Getting started

1. **Fork** the repository (`https://github.com/armory3d/armorpaint`) and clone your fork.
2. Create a feature branch: `git checkout -b my-feature`.
3. Make your changes, following the [code style](#code-style).
4. Build and test locally (see [readme.md](readme.md)).
5. Push your branch and open a **pull request**.

## Issues

Before opening an issue:

- Search [existing issues](https://github.com/armory3d/armorpaint/issues?q=is%3Aissue) and the [forums](https://forums.armorpaint.org/) for duplicates.
- Use the [bug report](.github/ISSUE_TEMPLATE/bug_report.md) or [feature request](.github/ISSUE_TEMPLATE/feature_request.md) templates.
- Include: commit hash (or version), OS/GPU model, steps to reproduce, and screenshots if applicable.

Compilation problems belong in the forums (https://forums.armorpaint.org/c/support), not the issue tracker.

## Code style

- C sources follow the style enforced by [`.clang-format`](.clang-format). Run `clang-format -i` on changed files before committing.
- Haxe sources (in `paint/sources` and `base/sources`) follow the existing conventions — match surrounding code.
- No trailing whitespace; final newline at end of file.
- Keep changes focused and reviewable. Prefer small, self-contained commits with descriptive messages.

## Workflow

- `main` is the integration branch. Pull requests must pass the [CI workflows](.github/workflows/).
- Commit message style: `area: short description` (e.g. `paint: fix layer merging undo`).
- Do not commit build artifacts or local configuration — `build*/` directories are gitignored.
- Rebase your branch on the latest `main` before opening/updating a PR.

## Building & testing

- See the [build instructions](readme.md#build) for your platform.
- Linux: `../base/make --run`
- Base engine tests live in [`base/tests`](base/tests); run them after touching low-level code.
- Report build/release artifacts under `paint/build/temp/` are gitignored and should never be committed.

## Community

- Development forum: https://forums.armorpaint.org/
- Manual: https://armorpaint.org/manual

By participating in this project you agree to abide by the [Code of Conduct](CODE_OF_CONDUCT.md).