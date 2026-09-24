[amake](https://github.com/armory3d/armorpaint/tree/main/base/tools/amake) is the armorpaint build tool (based on [kmake](https://github.com/Kode/kmake)). It consists of several parts:

`aimage.c`: Converts common image formats like `.jpg` / `.png` / `.hdr` into a custom `.k` format, which is faster to load. `.k` is a simple format which contains an image header and lz4 compressed pixel data. It also handles processing the `icon.png` file into a custom OS defined format (`.ico` on Windows).

[`ashader.c`](https://github.com/armory3d/armorpaint/blob/main/base/docs/ashader.md): Converts a `.shader` source into a graphics api specific format.

`make.c`: Evaluates `project.c` files with the embedded [minic](https://github.com/armory3d/armorpaint/blob/main/base/sources/libs/minic.c) interpreter, then exports assets and shaders. The api available to `project.c` files is documented in [`amake.h`](https://github.com/armory3d/armorpaint/blob/main/base/tools/amake/amake.h).

`script.c`: `amake --c <file.c> [args]` runs a C file with minic, e.g. [`extract_locales.c`](https://github.com/armory3d/armorpaint/blob/main/base/tools/extract_locales.c).

`exporters.c`: Creates project files for the desired target, e.g. a Visual Studio solution, an Xcode project or a makefile.
