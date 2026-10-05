# Lyra Installer 1.0.0

A native Windows installer for [Lyra](https://github.com/tomokuroki/lyra), written in C++17 and designed to match the official [Lyra website](https://lyralang.vercel.app/).

## Features

- self-contained installation with no Git, CMake, or compiler required on the target computer;
- official Lyra logo and website-inspired interface;
- optional standard library, documentation, examples, and C++ source code;
- optional user `PATH` integration;
- `.lyra` file association;
- examples shortcut;
- registration in Windows Installed Apps;
- complete per-user uninstaller with no administrator privileges required.

Installer 1.0.0 currently packages Lyra 3.0.0 from the repository's `main` branch. The installer UI and packaged documentation are English-only.

## Build

Requirements:

- Git;
- MinGW-w64 with `g++` and `windres`;
- PowerShell.

Run:

```powershell
.\build.ps1
```

To update the embedded Lyra source before building:

```powershell
.\build.ps1 -Refresh
```

The finished installer is written to `lyra-1.0.0.exe` in the project root.

## Brand assets

`assets/logo-mark.svg` is the official logo used by the Lyra website. Raster Windows assets are stored alongside it. If they need to be regenerated, run:

```powershell
python .\tools\generate_brand_assets.py
```

