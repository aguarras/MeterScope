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

// Component 4: LUFS-I Gated Integration (BS.1770) - architecture.md Core
// Components #4. Ported from loudness_meter_cockos.jsfx lines 396-419 (the
// `a>=0` gated-accumulation block inside the LUFS-M block-boundary code) and
// lines 299/301 (lufs_a_hist / db_hist allocation in Reset()).
//
// Also owns the 75-bin, 1dB-resolution LUFS-M display histogram (jsfx's
// `db_hist[75]`, architecture.md's SharedMeterSnapshot `lufsMHistBins`) -
// this is computed in lock-step with, and gated identically to, the 1024-bin
// LUFS-I gating histogram (same `a>=0` conditional in the JSFX source), so it
// lives here rather than as a separate component.

namespace meterscope
{
    class LufsIEngine
    {
    public:
        static constexpr int kNumBins = 1024;      // jsfx NUM_BINS
        static constexpr double kBinsPerDb = 10.0; // jsfx BINS_PER_DB
        static constexpr double kDbPerBin = 1.0 / kBinsPerDb;
        static constexpr int kDbHistSize = 75;     // jsfx db_hist[75]

        void reset() noexcept;

        // Called once per 100ms block, ONLY when the caller's Component 3
        // determined this block's LUFS-M value is valid (`lufsMSumRaw > 0 &&
        // winCnt >= BlockAccumulator::mWinCnt`) - matches the JSFX's outer
        // `lufs_m_sum > 0 && win_cnt >= m_win_cnt ? ( ... )` gate (lines
        // 396-423), inside which this entire block lives.
        //
        // `isPlayingOrRecording` reproduces the JSFX's own inner gate
        // (`WANT_INTEGRATED_ALWAYS || (play_state&1)`, WANT_INTEGRATED_ALWAYS
        // is a compile-time 0 in the shipped JSFX) - when false, this call is
        // a no-op and `lufsIDb`/`dbHist` simply retain their previous values
        // (freeze, not reset - matches the JSFX exactly, since the whole
        // `a>=0` block, including the histogram writes, only executes while
        // playing/recording).
        void pushMomentaryBlock(double lufsMDb, double lufsMSumRaw, bool isPlayingOrRecording) noexcept;

        double lufsIDb = -100.0;

        std::array<std::uint32_t, kDbHistSize> dbHist {};
        std::uint32_t dbHistMax = 0;

    private:
        std::array<std::uint64_t, kNumBins> binCount {};
        std::array<double, kNumBins> binEnergy {};
        double lufsASum = 0.0;
        std::uint64_t lufsASumCnt = 0;
    };
}
