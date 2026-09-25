# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Script tab: duplicate button, search, and "new script" button
- Script API improvements (paint, console)
- Search to edit stage
- "Delete unused" button to textures tab
- Camera to timeline
- Update current script on console run

### Changed
- Script tab improvements for editing workflow
- Console prompt fixes

### Fixed
- Grid snap fixes
- Mesh append and delete fixes
- Text to text node fixes
- Word wrap fixes
- UI deselect text exposed
- Linux: keep a stable `WM_CLASS` (app name) instead of deriving it from the window title,
  so the desktop entry and `StartupWMClass` match correctly under GNOME/Wayland

### Performance
- Mesh merge faster for reskin
- Increased time resolution (base + paint)
- String array join faster

### Legacy
- Double click to select word

## [0.9.x] - 2025

- See upstream release notes: https://github.com/armory3d/armorpaint/releases

## Earlier

For older releases see the upstream repository:
https://github.com/armory3d/armorpaint/releases