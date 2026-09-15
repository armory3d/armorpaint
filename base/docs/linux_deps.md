For the compilation to succeed you might need to install some additional packages.

For Debian/Ubuntu based distributions:
```
sudo apt install make clang libvulkan-dev libgtk-3-dev libssl-dev libxi-dev libxrandr-dev libxcursor-dev libasound2-dev
```

For Arch based distributions:
```
sudo pacman -S make clang vulkan-devel gtk3 openssl libxi libxrandr libxcursor alsa-lib
```

For Fedora based distributions:
```
sudo dnf install make clang vulkan-loader-devel vulkan-headers gtk3-devel openssl-devel libXi-devel libXrandr-devel libXcursor-devel libXinerama-devel libudev-devel alsa-lib-devel mesa-libGL-devel
```
