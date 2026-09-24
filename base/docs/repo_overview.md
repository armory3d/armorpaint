This repository contains everything needed to build armorpaint except the C compiler itself.

## `/base`

Contains a general-purpose 3D engine.

#### `/sources`

A collection of C source files for the complete 3D engine, hardware abstraction, OS and graphics api specifics (`/backends`) and some helper libraries (`/libs`). Only a minimal single file / header libraries are used.

#### `/tests`

Small stand-alone projects for testing.

#### `/tools`

- `/amake`: armorpain build tool
- `/bin`: amake binary for each supported platform
- `/tcc`: actually a C compiler

#### `/project.c`

This file adds C source files, include directories, libraries and defines to the project, depending on the selected platform, graphics api and build flags.

## `/paint`

Contains armorpaint specific code & assets only. The `project.c` file adds this specific code to the final project.
