# MeterScope on macOS

Universal build (Apple Silicon and Intel), macOS 10.15 or later, VST3 and Audio Unit.

> **Test build.** macOS builds are compiled automatically and signed ad-hoc (free, no Apple account). They are not
> notarized by Apple. Please report anything odd at https://github.com/aguarras/MeterScope/issues.

## Install: one command (recommended)

Open **Terminal** (Applications → Utilities), paste this line and press Enter:

```bash
curl -fsSL https://raw.githubusercontent.com/aguarras/MeterScope/main/scripts/install-macos.sh | bash
```

For a test (pre-release) build, add its tag at the end, for example `| bash -s v1.1.0-beta.1`.

It installs into your user folder (no administrator password):
- `~/Library/Audio/Plug-Ins/VST3/MeterScope.vst3`
- `~/Library/Audio/Plug-Ins/Components/MeterScope.component`

Then open REAPER and rescan plug-ins (Preferences → Plug-ins → VST → Re-scan).

## Install: manually (downloaded with a web browser)

1. Download `MeterScope-macOS.zip` from the [Releases](https://github.com/aguarras/MeterScope/releases) page and
   double-click it to unzip.
2. Copy `MeterScope.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `MeterScope.component` to
   `~/Library/Audio/Plug-Ins/Components/` (in Finder: Go → Go to Folder…).
3. A browser download is quarantined by macOS, so remove the flag in Terminal:
   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MeterScope.vst3 ~/Library/Audio/Plug-Ins/Components/MeterScope.component
   ```
4. Rescan plug-ins in your DAW.

## Uninstall

```bash
rm -rf ~/Library/Audio/Plug-Ins/VST3/MeterScope.vst3 ~/Library/Audio/Plug-Ins/Components/MeterScope.component
```

## Build it yourself

Install Xcode (App Store), CMake and Git, then:

```bash
git clone https://github.com/aguarras/MeterScope.git && cd MeterScope
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target MeterScope_VST3 MeterScope_AU
```

A plugin you build on your own Mac is not quarantined, so it loads without any extra step.

MeterScope is free software: AGPL-3.0-or-later (measurement engine: LGPL-3.0-or-later). See `NOTICE.md`.
