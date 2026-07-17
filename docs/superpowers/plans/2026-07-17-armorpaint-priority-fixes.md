# ArmorPaint Priority Issue Fixes Implementation Plan
> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the selected high-priority ArmorPaint issues with evidence-backed, low-risk guards and behavior fixes, then publish the work as a draft pull request.

**Architecture:** Keep fixes at the existing boundary where invalid persisted configuration, missing external assets, missing GPU textures, layer-limit failures, and history traversal are first observable. Avoid broad refactors or platform-specific workarounds that cannot be validated locally.

**Tech Stack:** C, ArmorPaint/Iron runtime, generated Visual Studio project files, PowerShell, Python source-regression checks, GitHub CLI/API.

## Global Constraints

- Preserve unrelated upstream changes and keep the branch narrowly scoped.
- Cover selected issues #2082, #2080, #2065, #2090, #792, #2052, #2062, and #2084 where the repository provides a defensible fix.
- Add only a small number of adjacent, clearly proven crash guards (especially failed layer creation and missing export presets/assets).
- Run tests before and after each implementation group where the local toolchain permits; document the missing native compiler if it remains unavailable.
- Do not claim Linux/GPU-specific reproduction when only static call-chain evidence is available.

## Tasks

- [ ] Add failing source-regression checks for configuration validation, opacity clamping, import/export null guards, 2D texture safety, layer creation limits, and bounded history traversal.
- [ ] Harden UI-scale loading/saving/application so malformed or negative persisted values cannot prevent startup.
- [ ] Clamp brush opacity after pressure/node multipliers before it reaches paint and cursor rendering.
- [ ] Make Blender import use a writable temporary path and stop cleanly when Blender does not produce the OBJ.
- [ ] Make packed-project/export paths reject missing assets/presets instead of dereferencing null data.
- [ ] Make 2D view tolerate layers without initialized paint textures and make layer creation failure-safe at the maximum layer count.
- [ ] Group layer-mask undo restoration for a delete operation and bound redo history scanning to prevent out-of-range access.
- [ ] Run regression checks, regenerate the ArmorPaint project, inspect the diff, commit the focused changes, push the branch, and open a draft PR.

## Verification Checklist

- [ ] Regression checks pass after the fixes.
- [ ] `base/make.bat --help` (project generation path used by this checkout) completes successfully.
- [ ] Native compilation or launch is attempted; any unavailable compiler/runtime limitation is recorded in the PR.
- [ ] Branch status, commit, remote branch, and draft PR are all verified directly.
