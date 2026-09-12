> **Community fork** — This repository is a fork of [armory3d/armorpaint](https://github.com/armory3d/armorpaint).
> It is **not the original project** and does not claim authorship of ArmorPaint. It contains modified
> source code derived from the original software, which is the work of the ArmorPaint developers.
> See [NOTICE](NOTICE) and [LICENSE](LICENSE).

![](https://armorpaint.org/img/git.jpg)

armorpaint
==============

[ArmorPaint](https://armorpaint.org) is a software for 3D PBR texture painting - check out the [manual](https://armorpaint.org/manual).

*Note 1: This repository is aimed at developers and may not be stable. Distributed binaries are [paid](https://armorpaint.org/download) to help with the project funding. All of the development is happening here in order to make it accessible to everyone. Thank you for support!*

*Note 2: If you are compiling git version of ArmorPaint, then you need to have a compiler ([Visual Studio with clang tools](https://visualstudio.microsoft.com/downloads/) - Windows, [clang + dependencies](https://github.com/armory3d/armorpaint/blob/main/base/docs/linux_deps.md) - Linux, [Xcode](https://developer.apple.com/xcode/resources/) - macOS / iOS, [Android Studio](https://developer.android.com/studio) - Android) and [git](https://git-scm.com/downloads) installed.*

```bash
git clone https://github.com/armory3d/armorpaint
cd armorpaint/paint
```

**Windows (x64)**
```bash
..\base\make
# Open generated Visual Studio project at `build\ArmorPaint.sln`
# Build and run
```

**Linux (x64)**
```bash
../base/make --run
```

**macOS (arm64)**
```bash
../base/make
# Open generated Xcode project at `build/ArmorPaint.xcodeproj`
# Build and run
```

**Android (arm64)**
```bash
../base/make --target android
# Open generated Android Studio project at `build/ArmorPaint`
# Build for device
```

**iOS (arm64)**
```bash
../base/make --target ios
# Open generated Xcode project `build/ArmorPaint.xcodeproj`
# Build for device
```

**WASM**
```bash
../base/make --target wasm --compile --embed
```

**Generating a locale file**
```bash
./base/make --js base/tools/extract_locales.js <locale code>
# Generates a `paint/assets/locale/<locale code>.json` file
```

**Embedding data files**
```bash
# Requires compiler with c23 #embed support (clang 19 or newer)
../base/make --embed
```

## License & attribution

ArmorPaint is distributed under the [**zlib/libpng license**](license.md) (see also [`LICENSE`](LICENSE)). This fork keeps the original license notice intact and must not remove or alter it in any distributed form.

Attribution and fork status:

- **Original project:** [armory3d/armorpaint](https://github.com/armory3d/armorpaint) — all copyright of the original and derived source belongs to the ArmorPaint developers.
- **Author of the original software:** Lubos Lenco (armory3d).
- **Fork:** [dionarley/armorpaint](https://github.com/dionarley/armorpaint) — modified version, not the original software.
- **Third-party components** bundled in the distributable are listed under [`paint/assets/licenses/`](paint/assets/licenses/) and [`base/assets/licenses/`](base/assets/licenses/) (Kore, NFD, fonts, icons, IRISc, kongruent, lz4-wasm, llama.cpp, UFBX, cgltf, nanosvg, ...).

Per the zlib license terms: the origin of this software must not be misrepresented, altered versions must be plainly marked as such, and this notice may not be removed or altered from any source distribution.

## Branches

This repository is a fork of [armory3d/armorpaint](https://github.com/armory3d/armorpaint). Below is an overview of the branches tracked by this repository.

### Upstream branches (`armory3d/armorpaint`)

| Branch    | Status            | Purpose                                                                 |
| --------- | ----------------- | ----------------------------------------------------------------------- |
| `main`    | active            | Line of development (Vulkan, Direct3D12, Metal, WebGPU)                  |
| `10`      | maintenance       | Version 1.0 line; revert of the `1.1alpha` version bump (`paint/sources/globals.h`) |
| `forge`   | experimental      | Development of the standalone "ArmorForge" variant                       |
| `g4`      | merged / periodic | GPU improvements and import fixes (glTF with binary data, file drop path on Windows) |
| `gc`      | merged            | Old development tip, fully absorbed into `main`                          |
| `haxe`    | historical        | Pre-C codebase written in Haxe                                           |
| `lab`     | experimental      | Research branch (e.g. `iron_load_url` fixes on iOS)                      |
| `sculpt`  | experimental      | Mesh sculpting work, related to the "ArmorForge" app                     |
| `slug`    | experimental      | "SLUG" renderer experiment (`base/shaders/draw_slug.kong`)               |
| `ts`      | experimental      | Viewport rewrite experiment in TypeScript (`paint/sources/viewport.ts`)  |
| `v8`      | historical        | Old branch of the project (V8/Javascript engine integration era)         |

### Fork branches (`dionarley/armorpaint`)

| Branch      | Status | Purpose                                                                 |
| ----------- | ------ | ----------------------------------------------------------------------- |
| `main`      | active | Default branch, kept in sync with upstream `main`                       |
| `build`     | active | Linux build tooling and dependency documentation                        |
| `opensource`| active | Repository polish: contributing, license, changelog, code of conduct    |
