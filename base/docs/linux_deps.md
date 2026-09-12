For the compilation to succeed you might need to install some additional packages.

For Debian/Ubuntu based distributions:
```
sudo apt install make clang libvulkan-dev libgtk-3-dev libssl-dev libxi-dev libxrandr-dev libxcursor-dev libasound2-dev
```

For Arch based distributions:
```
sudo pacman -S make clang vulkan-devel gtk3 openssl libxi libxrandr libxcursor alsa-lib
```

## USB rights for Android devices
```
sudo apt install android-tools-adb
adb devices
```

## Verified status

Dependencies checked on **Ubuntu 26.04 LTS (x86_64), clang 21** — all present:

| Dependency                | Status | Version tested                       |
| ------------------------- | ------ | ------------------------------------ |
| make                      | OK     | GNU Make 4.4.1                      |
| clang                     | OK     | Ubuntu clang 21.1.8 (6ubuntu1)       |
| gcc                       | OK     | (helper, only needed on some targets)|
| git                       | OK     | required for fresh checkout           |
| node                      | OK     | required by build scripts             |
| libvulkan-dev             | OK     | 1.4.341.0-1                          |
| libgtk-3-dev              | OK     | 3.24.52-0ubuntu1                     |
| libssl-dev                | OK     | 3.5.5-1ubuntu3.5                     |
| libxi-dev                 | OK     | 2:1.8.2-2                            |
| libxrandr-dev             | OK     | 2:1.5.4-1build1                      |
| libxcursor-dev            | OK     | 1:1.2.3-1build1                      |
| libasound2-dev            | OK     | 1.2.15.3-1ubuntu1.1                  |

> [!NOTE]
> `clang 19` (or newer) is only required when building with `--embed` (needs C23 `#embed` support).
> The `amake` build tool is bundled prebuilt at `base/tools/bin/<platform>/`, no manual install needed.