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
./base/make --c base/tools/extract_locales.c <locale code>
# Generates a `paint/assets/locale/<locale code>.json` file
```

**Embedding data files**
```bash
# Requires compiler with c23 #embed support (clang 19 or newer)
../base/make --embed
```
