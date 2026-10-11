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
#include <cstdint>

// Component 5: LRA Engine (EBU Tech 3342) - architecture.md Core Components #5.
// Ported from loudness_meter_cockos.jsfx lines 425-464 (the `b>=0` gated
// accumulation + percentile search inside the LUFS-S block-boundary code).
//
// A SECOND, independent 1024-bin histogram from Component 4's - count-only,
// no energy sum needed per bin (architecture.md: "a second, independent
// 1024-bin histogram, count-only"). The running "ungated average" used only
// for the relative gate is tracked via separate scalar accumulators
// (lufsBSum/lufsBSumCnt), matching jsfx's `lufs_b_sum`/`lufs_b_sum_cnt`
// (scalars) plus `lufs_b_hist[]` (1024-bin count array) as two parallel
// structures - NOT derived from the histogram itself.

namespace meterscope
{
    class LraEngine
    {
    public:
        static constexpr int kNumBins = 1024;      // jsfx NUM_BINS
        static constexpr double kBinsPerDb = 10.0; // jsfx BINS_PER_DB
        static constexpr double kDbPerBin = 1.0 / kBinsPerDb;

        void reset() noexcept;

        // Called once per 100ms block, ONLY when the caller's Component 3
        // determined this block's LUFS-S value is valid (`lufsSSumRaw > 0 &&
        // winCnt >= BlockAccumulator::sWinCnt`) - matches the JSFX's outer
        // `lufs_s_sum > 0 && win_cnt >= s_win_cnt ? ( ... )` gate.
        //
        // `isPlayingOrRecording` reproduces the inner
        // `WANT_INTEGRATED_ALWAYS || (play_state&1)` gate - when false, this
        // call is a no-op (lraLoDb/lraHiDb/lraDiff freeze, matching the JSFX).
        //
        // Errata #6a: if the gated count is < 20, lraLoDb/lraHiDb/lraDiff
        // retain their PREVIOUS values unchanged (not a sentinel reset) - this
        // is a genuine "freeze" behaviour, distinct from a Reset() clear.
        // Errata #6b: a negative relative-gate bin is clamped to 0 for every
        // use (the count loop AND both percentile searches), replicating the
        // JSFX's own empirically-all-zero out-of-bounds read rather than the
        // undefined-behaviour read itself.
        void pushShortTermBlock(double lufsSDb, double lufsSSumRaw, bool isPlayingOrRecording) noexcept;

        double lraLoDb = -100.0;
        double lraHiDb = -100.0;
        double lraDiff = 0.0;

    private:
        std::array<std::uint64_t, kNumBins> binCount {};
        double lufsBSum = 0.0;
        std::uint64_t lufsBSumCnt = 0;
    };
}
