// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// Derived from the "Loudness Meter Peak/RMS/LUFS" JSFX,
// Copyright (C) 2021 and later Cockos Incorporated (LGPL).
//
// SPDX-License-Identifier: LGPL-3.0-or-later
// This file is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See COPYING.LESSER and COPYING.

#pragma once

#include <atomic>
#include <array>
#include <cstdint>

// Phase 2.1 scope: the per-instance measurement snapshot shape only, matching
// architecture.md's SharedMeterRegistry::SharedMeterSnapshot struct (Multi-
// Instance Shared Store section). The registry/Entry wrapper that publishes
// this snapshot ACROSS instances (SharedMeterRegistry, ProjectKey, host
// integration) is explicitly Phase 2.2 scope and is NOT implemented here -
// this file only defines the snapshot's fields so a single instance's audio
// thread can publish into it (relaxed atomic stores, end of processBlock) and
// its own message thread / a future editor can read it back (relaxed atomic
// loads), per architecture.md's Thread Boundaries.
//
// Field defaults mirror the JSFX's own Reset() initial values / display floor
// sentinels: -72.0 for peak/true-peak (format_db's floor), -100.0 for
// RMS/LUFS values (jsfx's own -100 "not valid yet" sentinel), -100.0/0.0 for
// LRA lo/hi/diff (jsfx Reset(): `lra_db_diff=0; lra_db_hi=lra_db_lo=...=-100`).

namespace meterscope
{
    struct SharedMeterSnapshot
    {
        // Component 2 (shared peak/TP register, per channel). Both the peakX
        // and tpX field sets below are ALWAYS populated from the SAME shared
        // register, per architecture.md's "Peak/True-Peak Payload Correction"
        // decision (the JSFX genuinely has only one register per channel,
        // shared between the Peak and True-Peak families, never reset on a
        // mode switch - errata #7).
        std::atomic<double> peakL { -72.0 }, peakR { -72.0 };
        std::atomic<double> peakLHeld { -72.0 }, peakRHeld { -72.0 };
        std::atomic<int>    peakLClips { 0 },   peakRClips { 0 };

        std::atomic<double> tpL { -72.0 }, tpR { -72.0 };
        std::atomic<double> tpLHeld { -72.0 }, tpRHeld { -72.0 };
        std::atomic<int>    tpLClips { 0 },   tpRClips { 0 };

        // Components 3/4/5/6.
        std::atomic<double> rmsM { -100.0 }, rmsMHeld { -100.0 }, rmsI { -100.0 };
        std::atomic<double> lufsM { -100.0 }, lufsMHeld { -100.0 };
        std::atomic<double> lufsS { -100.0 }, lufsSHeld { -100.0 };
        std::atomic<double> lufsI { -100.0 };
        std::atomic<double> lraLo { -100.0 }, lraHi { -100.0 }, lraDiff { 0.0 };

        // jsfx's db_hist[75], 1dB bins covering -70..+4 (Component 4's
        // display-histogram side effect - see LoudnessHistogram.h).
        static constexpr int kLufsMHistBins = 75;
        std::array<std::atomic<std::uint32_t>, kLufsMHistBins> lufsMHistBins {};

        // TEMP (Phase 2.1 validation, loop-wrap test readout): number of full
        // Reset()s this engine has run since construction (prepare() included).
        // Remove together with the temporary editor readout in Stage 3.
        std::atomic<std::uint32_t> resetCount { 0 };

        // Bumped once per processBlock (relaxed) - lets readers detect
        // staleness cheaply without needing per-field change tracking.
        std::atomic<std::uint64_t> version { 0 };
    };
}
