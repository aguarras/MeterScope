# Changelog

All notable changes to MeterScope will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [1.1.0] - Unreleased

### Added

- **macOS build** (test): universal (Apple Silicon + Intel) VST3 and Audio Unit, macOS 10.15+. It is built
  automatically by GitHub Actions (`.github/workflows/build-macos.yml`), signed ad-hoc (free, not notarized), and
  checked with pluginval and auval.
- `scripts/install-macos.sh`: a one-command install for macOS, with no administrator password. See
  `README-macOS.md`.

## [1.0.1] - 2026-10-11

### Fixed

- **Indicator flashing (accessibility):** when a level hovered around an indicator threshold, the whole bar used to
  switch colour on every frame, flashing several times per second. That was distracting and a photosensitivity
  risk.
  - New default **Zone** style: only the part of the bar past the threshold (red) or inside the range (green) is
    coloured. The rest keeps the measurement's own colour.
  - **Whole bar** style is still available (Settings > Project > Indicator style). It is now stabilised: 0.5 dB
    hysteresis, a 350 ms minimum per colour state (at most ~1.4 flashes per second; the WCAG 2.3.1 limit is 3) and
    a 100 ms colour fade.
  - Display only: measurements are unchanged, and a bar still turns red on the first frame past the threshold.

## [1.0.0] - 2026-10-11

First release. VST3 + Standalone, Windows, vendor "TES Lab".

### Added

- **Measurements, bit-exact with the Cockos "Loudness Meter Peak/RMS/LUFS" JSFX:** Peak group (Off / True Peak /
  True Peak clips / Peak / Peak clips), LUFS-M (Off / On / On + histogram), RMS-M, RMS-I, LUFS-S, LRA and LUFS-I.
  All math runs in `double`, as in the JSFX. Audio passes through untouched.
- **Linked multi-instance operation:** one shared configuration per REAPER project. Any instance can be unlinked and
  given its own configuration.
- **Combined view (ALL TRACKS):** every instance of the project, in REAPER track order and labelled with the track
  name, with named groups, a group filter, solo/hide and per-box reset, link and Mono.
- **Per-project scoping:** instances in different REAPER project tabs never mix. Instances whose project can't be
  identified go into a separate partition and show a warning banner.
- **Level indicators** (Threshold / Range) inside each measurement row, with a per-meter cycle button.
- **Persistence:** the shared configuration is stored in the project's EXTSTATE. Editor size is stored per instance.
- WebView UI ("Graphite instrument" look with the brand gradient), 10% zoom, resizable window.

### Performance

- The editor shows its UI ~0.37 s after opening, instead of ~2.3 s, and is dark (never white) while it loads.
  This needs the PFS JUCE patch `patches/juce/0001-webview2-smartscreen-and-dark-fallback.patch`
  (critical pattern #24).

### Fixed (during Stage 4)

- **Host keyboard shortcuts:** after clicking the UI, Space and Enter drive the DAW transport again without
  clicking the timeline (critical pattern #25). In text fields (group names, indicator dB values), Enter confirms
  and Escape cancels.
- **Settings panel order:** the measurements are listed in meter order (Peak, RMS-M, RMS-I, LUFS-M, LUFS-S, LRA,
  LUFS-I).

### Validation

- JSFX comparison: 6/6 renders bit-exact (0 ULP at float32) for every metric, at block sizes 512 and 1111.
- pluginval strictness 10: SUCCESS.
- REAPER multi-instance and two-tab tests: see `.ideas/plan.md` (Phase 2.2 T1-T9, Phase 3.3 U1-U6). Stage 4
  regression on the final build (2026-10-11, 9/9 PASS): project load with the new plugin ID, U1-U5 (+T4/T5/T6),
  T8 tab close, Space/Enter shortcuts and the Standalone Unidentified banner. T1/T2/T9 need debug dump builds and
  weren't re-run, because the registry code hasn't changed since they passed. T7 was waived.

### Known limitations

- Windows only (VST3 + Standalone). There is no macOS/AU build.
- Multi-instance features rely on the REAPER API. In other hosts each instance works on its own (Unidentified banner).
- The UI needs the Microsoft Edge WebView2 Runtime (it ships with Windows 11).
- Licensing: MeterScope is a derivative work of an LGPL JSFX. See `NOTICE.md` before distributing.
