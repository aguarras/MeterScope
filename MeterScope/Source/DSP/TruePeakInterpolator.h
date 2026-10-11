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

#include <array>

// Component 2 (True-Peak sub-algorithm): the 32-tap windowed-sinc interpolator,
// ported line-for-line from loudness_meter_cockos.jsfx's sinc_gen_val/
// sinc_gen_slice (lines 98-122) and the true-peak branch of ch#.proc()
// (lines 174-187). architecture.md's "Algorithm Details" section explicitly
// asks for the sinc generation to be a small, independently unit-testable
// pure function, verified against hand-computed values BEFORE wiring it into
// the history-buffer dot product - see sincGenVal()/sincGenSlice() below.
//
// No JUCE class implements this specific kernel (juce::dsp::Oversampling is a
// different, polyphase-FIR algorithm with different coefficients and would NOT
// numerically match) - this is genuinely custom code, per architecture.md.

namespace meterscope
{
    constexpr int kTruePeakTapCount = 32;

    using SincTapSet = std::array<double, kTruePeakTapCount>;

    // Pure function - literal port of sinc_gen_val() (jsfx lines 98-103). The
    // JSFX's version is stateful (instance `slice_pos`, incremented as a side
    // effect on every call inside a loop); here it is restructured into a pure
    // function of the raw (un-centred) tap position `pos`, per architecture.md's
    // explicit request for "a pure sinc-generation function". Mathematically
    // identical to the JSFX's per-call behaviour: windowpos uses the RAW
    // position, sincpos uses the CENTRED position (pos-16) - do not centre both,
    // that is the JSFX's own asymmetry and an easy place to introduce a subtle
    // mismatch (architecture.md Component 2's explicit warning).
    double sincGenVal(double pos) noexcept;

    // Literal port of sinc_gen_slice() (jsfx lines 105-114): for a starting
    // fractional position `cs`, generates the 32 taps at raw positions
    // cs, cs+1, ..., cs+31.
    SincTapSet sincGenSlice(double cs) noexcept;

    // Per-channel true-peak interpolator: owns its own 32-sample history ring
    // (double, newest at index 0 - jsfx's this.h00 = lspl after the shift loop,
    // lines 178-182) and its own copy of the sinc coefficient sets. The JSFX
    // shares one global sinc.s1/s2/s3 across channels; duplicating the (pure,
    // sample-rate-only-dependent) coefficient computation per channel here is a
    // structural simplification that produces bit-identical coefficients - see
    // the Phase 2.1 report's "deviations/interpretations" list.
    class TruePeakInterpolator
    {
    public:
        // Recomputes the sinc coefficient set(s) for this sample rate and
        // selects the 4x (srate < 96000, 3 sets at cs=0.25/0.5/0.75) vs 2x
        // (srate >= 96000, 1 set at cs=0.5) oversampling branch - jsfx's
        // sinc_init() (lines 116-122).
        void prepare(double sampleRate) noexcept;

        // Clears the 32-sample history ring only (does not touch the sinc
        // coefficients - matches jsfx's ch#.init() history clear, called from
        // Reset() alongside, but logically separate from, sinc.sinc_init()).
        void reset() noexcept;

        // Shifts rawSample into the history ring and returns the interpolated
        // true-peak candidate for this sample: max(|h[16]|, |dot(h,s1)|, and -
        // only if srate < 96000 - |dot(h,s2)|, |dot(h,s3)|). Matches jsfx lines
        // 174-187 exactly, including using h[16] (16-sample-delayed raw sample),
        // NOT h[0] (current sample), as the "unfiltered" candidate (errata: this
        // is deliberate, not a bug).
        //
        // Per architecture.md errata #7: this history ring must only be pushed
        // while a True-Peak mode is active - the CALLER (PeakMeter / Component
        // 2's orchestration) is responsible for only invoking this method in
        // True-Peak modes, so switching into a TP mode correctly uses stale
        // (or zeroed, post-reset) history for the first 32 samples.
        double pushAndGetPeak(double rawSample) noexcept;

    private:
        static double dot(const std::array<double, kTruePeakTapCount>& history,
                           const SincTapSet& taps) noexcept;

        std::array<double, kTruePeakTapCount> history {}; // history[0] = newest
        SincTapSet s1 {}, s2 {}, s3 {};
        bool oversample4x = true; // true: srate < 96000 (4x, 3 sets); false: >= 96000 (2x, 1 set)
    };
}
