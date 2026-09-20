# Building ArmorPaint for Linux

This guide covers building the native **Vulkan (x86_64)** binary on Linux. The
same flow is exercised by CI in [`.github/workflows/linux_vulkan.yml`](../.github/workflows/linux_vulkan.yml).

## How the build works

`../base/make` runs the bundled **amake** tool (`base/tools/make.js`) against
`paint/project.js`. It:

1. **Exports** assets (`paint/assets/*`, locales, meshes, plugins, licenses) into
   `paint/build/out/data/`.
2. **Compiles** KONG shaders (`.kong`) to SPIR-V (`data/*.spirv`, `data/*.k`).
3. **Generates** a Makefile in `paint/build/Release/`.
4. `--compile` then runs `make -j$(nproc)` — `clang` compiles the iron engine
   (`base/sources`), the application (`paint/sources`) and the plugins
   (`paint/plugins`), and `clang++` links a single executable.

## Prerequisites

Debian / Ubuntu (verified on **Ubuntu 26.04 LTS, x86_64**):

```bash
sudo apt install make clang libvulkan-dev libgtk-3-dev libssl-dev libxi-dev libxrandr-dev libxcursor-dev libasound2-dev
```

Arch:

```bash
sudo pacman -S make clang vulkan-devel gtk3 openssl libxi libxrandr libxcursor alsa-lib
```

The `amake` build tool is bundled prebuilt at `base/tools/bin/<platform>/` —
no manual install needed. `node` is required by platform scripts.

## Build

From `paint/`:

```bash
../base/make             # export assets + generate project files (no C compilation)
../base/make --compile   # compile C sources and link the executable
```

Outputs:

| Path                          | Description                                        |
| ----------------------------- | -------------------------------------------------- |
| `paint/build/Release/ArmorPaint` | intermediate executable (build tree)             |
| `paint/build/out/ArmorPaint`     | final executable (copied by amake after linking) |
| `paint/build/out/data/`          | exported data dir the executable loads at runtime |
| `paint/build/temp/*.log`         | `armorpaint_build.log`, `armorpaint_compile.log` |

> `build/` directories are gitignored (`**/build*/`) — artifacts are never committed.

## Run

```bash
../base/make --run       # compiles (if needed) then launches build/out/ArmorPaint
# or directly:
./build/out/ArmorPaint
```

## Command-line reference

Run from `paint/` as `../base/make <flags>`:

| Flag           | Meaning                                                        |
| -------------- | -------------------------------------------------------------- |
| (none)         | export assets + generate project files                         |
| `--compile`    | also compile and link the executable                           |
| `--run`        | `--compile` then launch the binary                             |
| `--debug`      | build `Debug` config instead of `Release`                      |
| `--embed`      | embed data via `#embed` (requires clang 19+)                   |
| `--target`     | cross-build target (e.g. `wasm`, `android`, `ios`, `windows`)  |
| `--js FILE`    | run a build script (e.g. `base/tools/extract_locales.js`)      |

Feature switches on the Linux build (from `paint/project.js`):

- `WITH_KONG`, `WITH_PLUGINS` — KONG shader compiler, plugin system
- `WITH_PHYSICS`, `WITH_RAYTRACE` — physics and raytraced baking
- `WITH_BC7` (BC7 compression), `WITH_NFD` (native file dialog),
  `WITH_COMPRESS`, `WITH_IMAGE_WRITE`, `WITH_VIDEO_WRITE`,
  `WITH_AUDIO`, `WITH_EVAL`
- `IRON_VULKAN` — Vulkan graphics backend on Linux

## Troubleshooting

- **Missing headers** — `error: vkGetInstanceProcAddr` or missing `X11/Xcursor/Xrandr`
  headers: install the packages above and re-run.
- **`clang: command not found`** — install `clang` (or set `--ccompiler`).
- **Driver problems at runtime** — the Vulkan loader needs a working driver
  (`libvulkan_radeon.so` / `libvulkan_intel.so` / lavapipe). If the window never
  appears on a headless/VM box, install a software driver (e.g.
  `mesa-vulkan-drivers`) or check `vulkaninfo`.
- **Known warnings** — `base/sources/libs/stb_vorbis.c` emits one
  `-Wtautological-compare` warning; it is harmless and pre-existing.

## Verified build

| Field        | Value                                            |
| ------------ | ------------------------------------------------ |
| Commit       | `5feb5f7` (branch `feat/build-linux-docs`)        |
| Host         | Ubuntu 26.04 LTS (x86_64)                        |
| make         | GNU Make 4.4.1                                   |
| clang        | Ubuntu clang 21.1.8 (6ubuntu1)                   |
| node         | v22.22.1                                         |
| Command      | `../base/make --compile`                          |
| Result       | SUCCESS — ELF x86-64, stripped, ~2.7 MB           |
| Smoke test   | binary launches and keeps running (`timeout` kill, no crash) |