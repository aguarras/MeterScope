# MeterScope

Loudness, peak and RMS meter (VST3 + Standalone, Windows) by **TES Lab**. Its measurements are bit-exact with the
Cockos "Loudness Meter Peak/RMS/LUFS" JSFX that ships with REAPER, and its instances are linked, with a combined
view of every track in REAPER track order.

*Medidor de loudness, peak y RMS (VST3 + Standalone, Windows) de TES Lab. Sus mediciones son idénticas, bit a bit,
a las del JSFX de Cockos incluido en REAPER, y sus instancias están vinculadas, con una vista combinada de todas las
pistas.*

## Download

Installers are on the [Releases](../../releases) page. The UI needs the Microsoft Edge WebView2 Runtime, which
ships with Windows 11.

## Build from source

Requirements: Windows 10/11 x64, Visual Studio 2022 (Desktop C++), CMake 3.22+, Git, Ninja (optional).

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MeterScope_VST3 MeterScope_Standalone
```

CMake downloads JUCE 9.0.2 and applies `patches/juce/` automatically. To use your own patched JUCE checkout instead,
add `-DJUCE_PATH=C:/path/to/JUCE`. The result goes to `build/MeterScope/MeterScope_artefacts/Release/`.

## License

- `MeterScope/Source/DSP/` (derived from Cockos' LGPL JSFX): **LGPL-3.0-or-later**. See `COPYING.LESSER` and
  `COPYING`.
- Everything else, and the distributed binaries (which include JUCE under the AGPLv3): **AGPL-3.0-or-later**. See
  `LICENSE`.
- Third-party notices: [`NOTICE.md`](NOTICE.md).

MeterScope is not affiliated with Cockos Incorporated or Steinberg Media Technologies GmbH.
