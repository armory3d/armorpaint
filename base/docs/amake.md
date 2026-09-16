[amake](https://github.com/armory3d/armorpaint/tree/main/base/tools/amake) is the armorpaint build tool (based on [kmake](https://github.com/Kode/kmake)). It consists of several parts:

`aimage.c`: Converts common image formats like `.jpg` / `.png` / `.hdr` into a custom `.k` format, which is faster to load. `.k` is a simple format which contains an image header and lz4 compressed pixel data. It also handles processing the `icon.png` file into a custom OS defined format (`.ico` on Windows).

[`ashader.c`](https://github.com/armory3d/armorpaint/blob/main/base/docs/ashader.md): Converts a `.kong` shader source into a graphics api specific format.

`quickjs`: An embedded JavaScript engine, which is used to run the `make.js` file (see below).

`make.js`: Handles processing of `project.js` files and creating project files for desired target, e.g. a Visual Studio solution.
