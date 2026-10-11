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

// Component 1: K-Weighting Filter Cascade (architecture.md Core Components #1).
// Ported verbatim from loudness_meter_cockos.jsfx lines 222-250 (init_lufs_filters())
// and lines 209-217 (the direct-form-II difference equation inside ch#.proc()).
//
// Deliberately NOT using juce::dsp::IIR::Filter/Coefficients - see architecture.md
// Component 1's "JUCE Class" note: JUCE's IIR coefficient/rounding path does not
// match the JSFX's hand-coded direct-form-II bit-for-bit, and is single/float
// oriented by default. Everything here is plain `double`.

namespace meterscope
{
    // The 5 coefficients per biquad stage (a1/a2/b0/b1/b2 - a0 is always folded
    // in already, matching the JSFX's own normalized-by-a0 Stage 1). Stage 2 is
    // DELIBERATELY left non-normalized (b0=1, b1=-2, b2=1, not divided by its
    // own a0) - this is a genuine JSFX quirk (architecture.md Component 1,
    // "note b0/b1/b2 are NOT divided by a0 in stage 2"), not a bug to "fix".
    struct KWeightingCoefficients
    {
        double f1a1 = 0.0, f1a2 = 0.0, f1b0 = 0.0, f1b1 = 0.0, f1b2 = 0.0;
        double f2a1 = 0.0, f2a2 = 0.0, f2b0 = 1.0, f2b1 = -2.0, f2b2 = 1.0;
    };

    // Pure function - recompute whenever sampleRate changes (architecture.md:
    // "Recompute all 5 coefficients whenever sampleRate changes"). Unit-testable
    // in isolation against hand-computed values from the JSFX source.
    KWeightingCoefficients computeKWeightingCoefficients(double sampleRate);

    // Per-channel filter STATE only (p1/p2 per stage) - the coefficients are
    // shared across channels (computed once per sample-rate change) and passed
    // in on every call, matching the JSFX's own global f1a1/f1a2/.../f2b2
    // (per-channel state, global coefficients).
    class KWeightingFilter
    {
    public:
        void reset() noexcept
        {
            f1p1 = f1p2 = f2p1 = f2p2 = 0.0;
        }

        // Direct-form II per stage, exactly as loudness_meter_cockos.jsfx lines
        // 209-217: `p0 = x - a1*p1 - a2*p2; y = b0*p0 + b1*p1 + b2*p2; p2=p1; p1=p0;`
        // Do NOT substitute a transposed or direct-form-I topology - rounding
        // differs (architecture.md Component 1).
        double process(double x, const KWeightingCoefficients& c) noexcept
        {
            const double f1p0 = x - c.f1a1 * f1p1 - c.f1a2 * f1p2;
            double y = c.f1b0 * f1p0 + c.f1b1 * f1p1 + c.f1b2 * f1p2;
            f1p2 = f1p1;
            f1p1 = f1p0;

            const double f2p0 = y - c.f2a1 * f2p1 - c.f2a2 * f2p2;
            y = c.f2b0 * f2p0 + c.f2b1 * f2p1 + c.f2b2 * f2p2;
            f2p2 = f2p1;
            f2p1 = f2p0;

            return y;
        }

    private:
        double f1p1 = 0.0, f1p2 = 0.0;
        double f2p1 = 0.0, f2p2 = 0.0;
    };
}
