# MeterScope: Licensing and Third-Party Notices

MeterScope — loudness, peak and RMS meter
Copyright (C) 2026 aguarras (TES Lab)

Source code: see `sourceUrl` in the release notes or installer. The exact source for every released binary is the
matching tag (`vX.Y.Z`) in that repository.

> This file summarises the licences. It is not legal advice.

## MeterScope's own licences

| Part | Files | Licence |
|---|---|---|
| Measurement engine (DSP), derived from Cockos' JSFX | `Source/DSP/` | **LGPL-3.0-or-later** (`COPYING.LESSER` + `COPYING`) |
| Everything else (plugin, editor, REAPER bridge, UI) | `Source/` (except `DSP/`), `Source/ui/` | **AGPL-3.0-or-later** (`LICENSE`) |
| The plugin binary as a whole (it contains JUCE, which is used under the AGPLv3) | `MeterScope.vst3`, `MeterScope.exe` | **AGPL-3.0-or-later** |

Every source file states its licence in an `SPDX-License-Identifier` header.

What this means in plain words: anyone may use MeterScope (including commercially), study it, modify it and
redistribute it. Whoever redistributes it, modified or not, must keep these licences and offer the complete source
code of what they distribute.

## Cockos "Loudness Meter Peak/RMS/LUFS" JSFX

MeterScope's measurement engine is a direct algorithmic port of:

```
Loudness Meter Peak/RMS/LUFS (Cockos)
This effect Copyright (C) 2021 and later Cockos Incorporated
License: LGPL - http://www.gnu.org/licenses/lgpl.html
```

The original source ships with REAPER (`Effects/analysis/loudness_meter`). MeterScope is not affiliated with or
endorsed by Cockos Incorporated. REAPER is a trademark of Cockos Incorporated.

## JUCE

JUCE 9.0.2 (https://juce.com), Copyright (c) Raw Material Software Limited, **used under the AGPLv3**
(https://www.gnu.org/licenses/agpl-3.0.en.html). MeterScope applies a small patch to JUCE's Windows WebView2
backend (`patches/juce/0001-webview2-smartscreen-and-dark-fallback.patch`), published here under the same
licence. `Source/ui/public/js/juce/` contains JUCE's own JavaScript bridge (same licence).

## Steinberg VST3 SDK

Copyright (c) 2025 Steinberg Media Technologies GmbH, MIT License (bundled with JUCE). VST is a registered
trademark of Steinberg Media Technologies GmbH.

## REAPER VST3 extension interface

`Source/Host/reaper_vst3_interfaces.h` is Cockos' declaration of `IReaperHostApplication`. It was taken verbatim
from the copy JUCE ships in `examples/Plugins/extern/`, and it is needed to talk to REAPER.

## Microsoft Edge WebView2

The UI runs on the Microsoft Edge WebView2 Runtime installed on the user's system (Microsoft's terms). The
statically linked WebView2 SDK loader (NuGet `Microsoft.Web.WebView2` 1.0.3485.44) is Copyright (C) Microsoft
Corporation under a BSD-style licence.
