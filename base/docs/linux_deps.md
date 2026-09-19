If compilation fails with `make: clang: No such file or directory`, clang is not installed. Install it using the packages below.

For the compilation to succeed you might need to install some additional packages.

For Debian/Ubuntu based distributions:
```
sudo apt install make clang libvulkan-dev libgtk-3-dev libssl-dev libxi-dev libxrandr-dev libxcursor-dev libasound2-dev
```

For Arch based distributions:
```
sudo pacman -S make clang vulkan-devel gtk3 openssl libxi libxrandr libxcursor alsa-lib
```
