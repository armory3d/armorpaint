# ArmorPaint

Full developer documentation generated with [Doxygen](https://www.doxygen.nl).

ArmorPaint is a **3D PBR texture painting** tool with a layer-based, real-time
workflow. This site documents the native C engine:

- `base/` - the **iron** engine (Vulkan / Direct3D12 / Metal / WebGPU backends,
  GPU, UI, audio, physics, math, image I/O, JSON, string/array utilities).
- `paint/` - the ArmorPaint application (painting, layers, nodes, brushes,
  materials, export/import, plugins, script API).

## Fork status

This repository is a **community fork** of
[armory3d/armorpaint](https://github.com/armory3d/armorpaint). It is not the
original project and does not claim authorship. See `NOTICE`, `LICENSE` and
`license.md` (zlib/libpng license).

## Building

See the [build instructions](https://github.com/dionarley/armorpaint#build).

```
cd paint
../base/make --run        # Linux: compile + run
../base/make --compile    # compile only
```

## Source layout

| Directory          | Contents                                                   |
| ------------------ | ---------------------------------------------------------- |
| `base/sources`     | iron engine (backends under `base/sources/backends`)       |
| `base/shaders`     | KONG shaders (`.kong`) compiled at build time              |
| `base/tools`       | `amake` build system, exporters, platform scripts          |
| `paint/sources`    | ArmorPaint application source                              |
| `paint/shaders`    | application KONG shaders                                   |
| `paint/plugins`    | plugin C source (`io_*`, `uv_unwrap`, `raytrace`, ...)     |
| `paint/assets`     | embedded assets (fonts, locales, icons, meshes, licenses)  |
| `base/docs`        | engine notes (`linux_deps.md`, `repo_overview.md`, ...)    |

## Generating the documentation

```bash
# dependencies: doxygen, graphviz (dot)
doxygen Doxyfile
# open: docs/doxygen/html/index.html
```

The generated pages are not committed; CI builds and publishes them to
GitHub Pages.