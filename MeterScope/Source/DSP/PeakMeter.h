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

#include "TruePeakInterpolator.h"

// Component 2: Sample-Peak / True-Peak Metering Engine (architecture.md Core
// Components #2). Owns the ONE shared per-channel {pkval, hipkval, clips}
// register set, written by whichever sub-algorithm `peakMode` selects - these
// registers are NEVER reset on a peakMode change (errata #7), only by the
// Reset & Transport Controller (Component 6).

namespace meterscope
{
    // Mirrors parameter-spec.md's `peakMode` Choice order exactly, which is
    // already identical to the JSFX's own cfg_peak slider order (0=off,
    // 1=true peak, 2=true peak clips, 3=peak, 4=peak clips).
    enum class PeakMode : int
    {
        Off = 0,
        TruePeak = 1,
        TruePeakClips = 2,
        Peak = 3,
        PeakClips = 4
    };

    struct PeakChannelState
    {
        double pkval = 0.0;    // decaying peak (Component 7 applies decay once per block)
        double hipkval = 0.0;  // held max - monotonic, cleared only by reset
        int clips = 0;         // clip counter - cleared only by reset
        TruePeakInterpolator tp;

        void prepare(double sampleRate) noexcept { tp.prepare(sampleRate); }

        void reset() noexcept
        {
            pkval = 0.0;
            hipkval = 0.0;
            clips = 0;
            tp.reset();
        }

        // jsfx ch#.decay(): this.pkval *= pk_decay; (Component 7 - once per
        // block, before the per-sample loop, using the actual block size).
        void decay(double pkDecay) noexcept { pkval *= pkDecay; }
    };

    // Ported from loudness_meter_cockos.jsfx lines 174-201 (the peak block
    // inside ch#.proc()). `rawSample` MUST be the raw, unweighted sample
    // (before K-weighting) - Processing Order Requirements, architecture.md.
    //
    // Only the sub-algorithm matching `mode` runs at all (errata #7): True
    // Peak's 32-tap sinc + history is evaluated only for TruePeak/
    // TruePeakClips, and the history is therefore only ADVANCED in those two
    // modes (TruePeakInterpolator::pushAndGetPeak is only called then) - a
    // mode switch into True Peak correctly sees stale (or post-reset zeroed)
    // history for its first 32 samples, exactly as the JSFX does.
    void processPeakSample(PeakChannelState& state, PeakMode mode, double rawSample) noexcept;
}
